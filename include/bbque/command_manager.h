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

#ifndef BBQUE_COMMAND_MANAGER_H_
#define BBQUE_COMMAND_MANAGER_H_

#include "bbque/config.h"

#include "bbque/utils/utility.h"
#include "bbque/utils/worker.h"
#include "bbque/utils/logging/logger.h"

#include <poll.h>
#include <csignal>
#include <thread>

#define COMMAND_MANAGER_NAMESPACE "bq.cm"

#define BBQUE_CMDS_FIFO "bbque_cmds"

namespace bu = bbque::utils;

namespace bbque {


/**
 * @brief The Interface for Command Handlers registration
 *
 * Classes which want to register a command handler must implement this
 * interface. Basically, a command hander is required to provide an
 * implementation for a @see CommandsCb callback method, which is called each
 * time a corresponding registered command is received by the @see
 * CommandManager module.
 */
class CommandHandler {
public:

	/**
	 * @brief Default destructor
	 */
	virtual ~CommandHandler() {};

	/**
	 * @brief The Callback method of a CommandHandler
	 *
	 * This is the method called for a corresponding registered command,
	 *
	 * @param argc number of parameters for the callback command
	 * @param argv[] parameter for the callback command
	 *
	 * @return 0 on success, a negative number otherwise
	 */
	virtual int CommandsCb(int argc, char *argv[]) = 0;

};

/**
 * @brief The Barbeque command management module
 *
 * This class provides a unified interface to register handlers for system
 * commands and to properly dispatch signals to them once a signal is sent to
 * the BarbequeRTRM.
 */
class CommandManager : public bu::Worker, public CommandHandler {

public:

	/**
	 * @brief Return a reference to the CommandManager module
	 *
	 * @return a reference to the CommandManger module
	 */
	static CommandManager & GetInstance();

	/**
	 * @brief Register a new command
	 *
	 * @param cmdName the name of the command
	 * @param cb the command callback, which will provide the actual
	 * 	command implementation.
	 * @param cmdDesc a description for the command, which is used to
	 * 	print the help.
	 *
	 * @return 0 on success, a negarive number otherwise.
	 */
	int RegisterCommand(const char *cmdName, CommandHandler *pch,
			const char *cmdDesc);

	/**
	 * @brief Unregister the specified command
	 *
	 * If a command with the specified cmdName has been previously
	 * reigstered, this command will clean-up it.
	 *
	 * @param cmdName the name of the command to unregister
	 *
	 * @return 0 on success, a negative number otherwise.
	 */
	int UnregisterCommand(const char *cmdName);

private:

	/**
	 * @brief Thrue if the channel has been correctly initalized
	 */
	bool initialized;

	/**
	 * @brief The path of the directory for FIFOs creation
	 */
	std::string cmd_fifo_dir;

	/**
	 * @brief The Commands server FIFO descriptor
	 */
	int cmd_fifo_fd;

	/**
	 * @brief The Commands server FILE descriptor
	 */
	FILE *cmd_fifo;

	/**
	 * @brief Support for POSIX polling the FIFO
	 */
	struct pollfd fifo_poll;

	/**
	 * @brief Support for getting POSIX signals on the FIFO
	 */
	sigset_t sigmask;

	/**
	 * @brief CommandManager module constructor
	 */
	CommandManager();

	/**
	 * @brief CommandManager module destructor
	 */
	~CommandManager();

	/**
	 * @brief Initialize the command reception FIFO
	 *
	 * This configure and properly initialize the FIFO channel to receive commands.
	 *
	 * @return 0 on success, a negative number otherwise
	 */
	int InitFifo();

	/**
	 * @brief The commands dispatcher thread
	 */
	void Task();

	/**
	 * @brief Preliminary operations before terminating the dispatcher thread
	 */
	void _PreTerminate();

	/**
	 * @brief Parse the specified command line
	 *
	 * This function provides the required support to parse a command line
	 * to produce a pair of argc and argv parameters, which are then
	 * dispatched to the argv[0] command.
	 *
	 * @note The implementation of this function is platform dependent, in
	 * a standard POSIX system which has WORDEXP support the expansion is
	 * done by a call to the standard POSIX wordexp call. Otherwise, for
	 * example in an Android system, a compatibility expansion routine is
	 * provided which however supports just single and double quotes and
	 * any one of the other advanced wordexp facilities.
	 */
	void ParseCommand(char *cmd_buff);

	/**
	 * @brief Simple command line parser
	 *
	 * This command line parser is a replacement of wordexpr for platform
	 * not supporting that feature, e.g. Android. Of course it is much
	 * more simple, indeed it support only up to 16 command line
	 * parameters, which are expected to be space separated.
	 * A single parameter could contain spaces if wrapped with single or
	 * double quotes. Quoting with a symbol allows to use the other quote
	 * simple within the parameter.
	 *
	 * @param cmdline the command line to be parsed
	 * @param maxargs maximum number of command line arguments
	 * @param argv[] the array of identified parameters
	 *
	 * @return the value of argc, i.e. the number of parsed parameters
	 */
	int ParseCommandLine(char *cmdline, int maxargs, char *argv[]);

	/**
	 * @brief Command dispatcher
	 *
	 * Once a new command is available, this method provides the proper
	 * handling code to dispatch it to the proper executor.
	 *
	 * @return 0 on success, a negative number otherwise
	 */
	int DoNextCommand();

	/**
	 * @brief Dispatch a command to the registered handler
	 *
	 * This check if the command defined by argv[0] has been registered
	 * and in case call the corresponding handler.
	 *
	 * @param argc the number of command parameres
	 * @param argv[] the command parameters
	 *
	 * @return 0 on success, a negative number otherwise.
	 */
	int DispatchCommand(int argc, char *argv[]);

	/**
	 * @brief Command handler for the CommandManager
	 *
	 * This callback implement support for these commands:<br>
	 * - bq.cm.help: dump a list of currently registered commands
	 *
	 */
	int CommandsCb(int argc, char *argv[]);
};

} // namespace bbque

#endif // BBQUE_COMMAND_MANAGER_H_
