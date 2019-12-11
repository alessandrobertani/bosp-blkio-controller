/*
 * Copyright (C) 2012  Politecnico di Milano
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "bbque/command_manager.h"

#include "bbque/configuration_manager.h"
#include "bbque/utils/utility.h"

#include <boost/filesystem.hpp>

#include <sys/stat.h>
#ifdef ANDROID
# include "bbque/android/ppoll.h"
# include "bbque/android/getline.h"
#else
# include <poll.h>
# include <stdio.h>
# include <wordexp.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <string>

// The prefix for configuration file attributes
#define MODULE_CONFIG "CommandManager"
#define MODULE_NAMESPACE COMMAND_MANAGER_NAMESPACE

namespace fs = boost::filesystem;
namespace po = boost::program_options;

namespace bbque
{

/******************************************************************************
 * Supported Commands
 ******************************************************************************/

/**
 * @struct bbqCommand
 * @brief A descriptor for a registered command
 */
typedef struct bbqCommand {
	const char *name; /** Name of the registered command */
	const char *desc; /** Desccription of the registered command */
	CommandHandler *pch; /** A reference to a CommandHander */
} bbqCommand_t;

/**
 * @brief Map of registered commands
 */
typedef std::map<std::string, bbqCommand_t> CommandsMap_t;

/**
 * @brief Map of the registered commands
 */
CommandsMap_t mCommands;

/**
 * @brief A mutex protecting access to the map of registered commands
 */
std::mutex mCommands_mtx;


int CommandManager::CommandsCb(int argc, char *argv[])
{
	uint8_t num = 0;
	// Fix compiler warnings
	(void) argc;
	(void) argv;

	logger->Notice("List of supported commands:");
	for_each(mCommands.begin(), mCommands.end(),
	[ =, &num](std::pair<std::string, bbqCommand_t> entry) {
		logger->Notice("%-25s : %s",
		               entry.second.name, entry.second.desc);
	});

	return 0;
}


/******************************************************************************
 * Commands Manager Module
 ******************************************************************************/

CommandManager::CommandManager() : Worker(),
	initialized(false),
	cmd_fifo_fd(0),
	cmd_fifo(NULL)
{
	//---------- Setup Worker
	Worker::Setup(BBQUE_MODULE_NAME("cm"), COMMAND_MANAGER_NAMESPACE);

	//---------- Loading module configuration
	ConfigurationManager & cm = ConfigurationManager::GetInstance();
	po::options_description opts_desc("Command Manager Options");
	opts_desc.add_options()
	(MODULE_CONFIG".dir",
	 po::value<std::string>
	 (&cmd_fifo_dir)->default_value(BBQUE_PATH_VAR),
	 "Path of the Command FIFO directory")
	;
	po::variables_map opts_vm;
	cm.ParseConfigurationFile(opts_desc, opts_vm);

	// Load all the supported commands
	RegisterCommand("bq.cm.help", static_cast<CommandHandler*>(this),
	                "List all the supported commands");

	// Setup FIFO channel
	if (InitFifo() == 0)
		initialized = true;
	if (!initialized) {
		logger->Fatal("Command interface initialization FAILED");
		Terminate();
	}

	// Start the command dispatching thread
	Worker::Start();
}

void CommandManager::_PreTerminate()
{
	fs::path fifo_path(cmd_fifo_dir);
	fifo_path /= "/" BBQUE_CMDS_FIFO;

	// Clean-up the command pipe
	logger->Debug("CMD MNGR: cleaning up FIFO...");
	if (ftell(cmd_fifo) >= 0) {
		fflush(cmd_fifo);
		fclose(cmd_fifo);
	}
	close(cmd_fifo_fd);
	unlink(fifo_path.string().c_str());
}

CommandManager::~CommandManager()
{

}

CommandManager & CommandManager::GetInstance()
{
	static CommandManager instance;
	return instance;
}

int CommandManager::RegisterCommand(const char *cmdName, CommandHandler *pch,
                                    const char *cmdDesc)
{
	std::unique_lock<std::mutex> mCommands_ul(mCommands_mtx);

	logger->Info("Registering command [%s]: %s", cmdName, cmdDesc);

	if (mCommands.find(cmdName) != mCommands.end()) {
		logger->Error("Register command FAILED"
		              " (Error: Command [%s] already defined)",
		              cmdName);
		return -1;
	}

	mCommands[cmdName] = {cmdName, cmdDesc, pch};
	return 0;
}


int CommandManager::UnregisterCommand(const char *cmdName)
{
	std::unique_lock<std::mutex> mCommands_ul(mCommands_mtx);

	logger->Info("Unregistering command [%s]...", cmdName);
	mCommands.erase(cmdName);

	return 0;
}

int CommandManager::InitFifo()
{
	int error;
	fs::path fifo_path(cmd_fifo_dir);
	boost::system::error_code ec;

	logger->Debug("CMD MNGR: FIFO channel initialization...");

	if (daemonized)
		syslog(LOG_INFO, "Using Command FIFOs dir [%s]",
		       cmd_fifo_dir.c_str());
	else
		fprintf(stderr, FI("CMD MNGR: using dir [%s]\n"),
		        cmd_fifo_dir.c_str());

	fifo_path /= "/" BBQUE_CMDS_FIFO;
	logger->Debug("CMD MNGR: checking FIFO [%s]...",
	              fifo_path.string().c_str());

	// If the FIFO already exists: destroy it and rebuild a new one
	if (fs::exists(fifo_path, ec)) {
		logger->Debug("CMD MNGR: destroying old FIFO [%s]...",
		              fifo_path.string().c_str());
		error = ::unlink(fifo_path.string().c_str());
		if (error) {
			logger->Crit("CMD MNGR: cleanup old FIFO [%s] FAILED "
			             "(Error: %s)",
			             fifo_path.string().c_str(),
			             strerror(error));
			assert(error == 0);
			return -1;
		}
	}

	// Make dir (if not already present)
	logger->Debug("CMD MNGR: create dir [%s]...",
	              fifo_path.parent_path().c_str());
	fs::create_directories(fifo_path.parent_path(), ec);

	// Create the server side pipe (if not already existing)
	logger->Debug("CMD MNGR: create FIFO [%s]...",
	              fifo_path.string().c_str());
	error = ::mkfifo(fifo_path.string().c_str(), 0666);
	if (error) {
		logger->Error("CMD MNGR: FIFO [%s] cration FAILED"
		              " (Error: unable to create file entry)",
		              fifo_path.string().c_str());
		return -2;
	}

	// Ensuring we have a pipe
	if (fs::status(fifo_path, ec).type() != fs::fifo_file) {
		logger->Error("CMD MNGR: FIFO [%s] creation FAILED"
		              " (Error: already in use)",
		              fifo_path.string().c_str());
		return -3;
	}

	// Opening the server side pipe (R/W to keep it opened)
	logger->Debug("CMD MNGR: opening FIFO R/W...");
	cmd_fifo_fd = ::open(fifo_path.string().c_str(), O_RDWR);
	if (cmd_fifo_fd == 0) {
		logger->Error("CMD MNGR: opening FIFO [%s] FAILED"
		              "(Error %c: %s)",
		              fifo_path.string().c_str(),
		              strerror(errno), errno);
		::unlink(fifo_path.string().c_str());
		return -4;
	}

	// Ensuring the FIFO is R/W to everyone
	if (::fchmod(cmd_fifo_fd, S_IRUSR | S_IWUSR | S_IWGRP | S_IWOTH)) {
		logger->Error("CMD MNGR: set permissions on FIFO [%s] FAILED"
		              "(Error %d: %s)",
		              fifo_path.string().c_str(),
		              errno, strerror(errno));
		::close(cmd_fifo_fd);
		::unlink(fifo_path.string().c_str());
		return -5;
	}

	logger->Debug("CMD MNGR: opening FIFO stream...");
	cmd_fifo = ::fdopen(cmd_fifo_fd, "r");

	// Setup FIFO signal mask
	fifo_poll.fd = cmd_fifo_fd;
	fifo_poll.events = POLLIN;
	sigemptyset(&sigmask);

	logger->Info("CMD MNGR: FIFO channel initialization DONE");
	return 0;
}

void CommandManager::Task()
{
	int ret;
	logger->Info("Task: Commands dispatcher STARTED");

	while (!done) {
		logger->Debug("Task: Waiting command...");
		ret = ::ppoll(&fifo_poll, 1, NULL, &sigmask);
		if (ret < 0) {
			logger->Debug("Task: Communication interrupted");
			continue;
		}

		DoNextCommand();
	}
	logger->Info("Task: Commands dispatcher terminated");
}

int CommandManager::DispatchCommand(int argc, char *argv[])
{
	std::unique_lock<std::mutex> mCommands_ul(mCommands_mtx);
	CommandsMap_t::iterator it;

	if (argc < 1) {
		logger->Error("CMD MNGR: Dispatching command FAILED"
		              " (Error: missing command)");
		return -1;
	}

	logger->Debug("CMD MNGR: Dispatching command [%s]", argv[0]);
	for (uint8_t i = 1; i < argc; ++i)
		logger->Debug("CMD MNGR: \tParam[%02d] [%s]", i, argv[i]);

	// Dispatch command to its handler
	it = mCommands.find(argv[0]);
	if (it == mCommands.end()) {
		logger->Error("CMD MNGR: Dispatching command FAILED"
		              " (Error: command [%s] not suppported)",
		              argv[0]);
		return -2;
	}

	return (it->second).pch->CommandsCb(argc, argv);
}


#ifdef ANDROID

// The maximum number of args we support (on the Android platform)
# define MAX_ARGC 16

void CommandManager::ParseCommand(char *cmd_buff)
{
	char *myargv[MAX_ARGC];
	int myargc;

	logger->Debug("Parsing command line [%s]\n", cmd_buff);
	myargc = ParseCommandLine(cmd_buff, MAX_ARGC, myargv);

	logger->Debug("Parsed %02d parameters:\n", myargc);
	for (uint8_t i = 0; i < myargc; ++i)
		logger->Debug("Arg%02d: [%s]\n", i, myargv[i]);

	DispatchCommand(myargc, myargv);
}

#else

void CommandManager::ParseCommand(char *cmd_buff)
{
	ssize_t result = 0;
	wordexp_t we;

	result = wordexp(cmd_buff, &we, WRDE_SHOWERR | WRDE_UNDEF);
	switch(result) {
	case 0:
		break;
	case WRDE_BADCHAR:
		logger->Error("Command parsing FAILED"
		              " (Error: Illegal occurrence of newline or one of |, &, ;, <, >, (, ), {, })");
		return;
	case WRDE_BADVAL:
		logger->Error("Command parsing FAILED"
		              " (Error: An undefined shell variable was referenced)");
		return;
	case WRDE_CMDSUB:
		logger->Error("Command parsing FAILED"
		              " (Error: Command substitution occurred)");
		return;
	case WRDE_NOSPACE:
		logger->Error("Command parsing FAILED"
		              " (Error: Out of memory)");
		return;
	case WRDE_SYNTAX:
		logger->Error("Command parsing FAILED"
		              " (Error: Shell syntax error)");
		return;
	}

	DispatchCommand(we.we_wordc, we.we_wordv);
	wordfree(&we);
}

#endif

int CommandManager::DoNextCommand()
{
	char *cmd_buff = NULL;
	ssize_t result = 0;
	size_t len;

	// Read next command being available
	result = ::getline(&cmd_buff, &len, cmd_fifo);
	if (result <= 0) {
		if (result != EINTR && !done)
			logger->Error("CMD MNGR: fifo read error");
	} else if (!done) {
		// Removing trailing "\n"
		if (cmd_buff[result - 1] == '\n')
			cmd_buff[result - 1] = 0;

		// Parsing command
		logger->Debug("CMD MNGR: parsing command [%s]", cmd_buff);
		ParseCommand(cmd_buff);
	}

	free(cmd_buff);
	return result;
}

//------------------------------------------------------------------------------
// Custom Command Line Parser
//------------------------------------------------------------------------------

enum {
	DEFAULT = 0,
	DOUBLE_QUOTE,
	SINGLE_QUOTE
};

static const char *statusStr[] = {
	"ARGUMENT",
	"DOUBLE_QUOTE",
	"SINGLE_QUOTE"
};

int CommandManager::ParseCommandLine(char *cmdline, int maxargs, char *argv[])
{
	uint8_t len = strlen(cmdline);
	uint8_t statusId = DEFAULT;
	char *pstart, *pend;
	const char *delim;
	int argc;

	logger->Debug("Parsing command line [%s]\n", cmdline);

	// blow-up inital blanks
	while (*cmdline && *cmdline == ' ')
		++cmdline;

	argc = 0;
	pstart = NULL;
	do {
		if (argc >= maxargs)
			return argc;

		statusId = DEFAULT;
		delim = " ";

		// Lookup for pos token being an escape
		if (pstart == NULL)
			pstart = cmdline - 1;
		else
			pstart += strlen(pstart);

		// Find beginning of next token
		do {
			++pstart;
			logger->Debug("   Lookup: [%c]\n", *pstart);
			switch (*pstart) {
			case '\"':
				statusId = DOUBLE_QUOTE;
				delim = "\"";
				break;

			case '\'':
				statusId = SINGLE_QUOTE;
				delim = "\'";
				break;
			}
		} while (pstart && *pstart == ' ');
		if (delim[0] != ' ')
			++pstart;

		logger->Debug("   Following: %s, delim: [%c]\n   => [%s]\n",
		              statusStr[statusId], delim[0], pstart);

		// Isolate pos token
		pend = pstart;
		while (*pend && *pend != delim[0])
			++pend;
		if (pend == pstart)
			break;
		*pend = '\0';

		// Keep track of the new parameter
		argv[argc++] = pstart;
		logger->Debug("Arg%02d: [%s]\n", argc, pstart);

	} while (abs(pend - cmdline) < len);

	return argc;

}

} /* bbque */
