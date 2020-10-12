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

#include "bbque/config.h"

#include "bbque/utils/logging/logger.h"

#include "bbque/utils/logging/console_logger.h"
#ifdef CONFIG_EXTERNAL_LOG4CPP
# include "bbque/utils/logging/log4cpp_logger.h"
#elif defined CONFIG_TARGET_ANDROID
# include "bbque/utils/logging/android_logger.h"
#endif

namespace bbque { namespace utils {

std::string Logger::conf_file_path = BBQUE_PATH_PREFIX "/" BBQUE_PATH_CONF "/" BBQUE_CONF_FILE;

Logger::Logger(Configuration const & conf) :
	configuration(conf) {

}

std::unique_ptr<Logger> Logger::GetLogger(Configuration const & conf) {
	std::unique_ptr<Logger> logger;
#ifdef CONFIG_EXTERNAL_LOG4CPP
	logger = Log4CppLogger::GetInstance(conf);
#elif defined CONFIG_TARGET_ANDROID
	logger = AndroidLogger::GetInstance(conf);
#else
#endif
	// Since this is a critical module, a fall-back dummy (console based) logger
	// implementation is always available.
	if (!logger) {
		logger = ConsoleLogger::GetInstance(conf);
		logger->Error("Logger module loading/configuration FAILED");
		logger->Warn("Using (dummy) console logger");
	}
	return logger;
}

} // namespace utils

} // namespace bbque
