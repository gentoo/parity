/****************************************************************\
*                                                                *
* Copyright (C) 2007 by Markus Duft <markus.duft@salomon.at>     *
*                                                                *
* This file is part of parity.                                   *
*                                                                *
* parity is free software: you can redistribute it and/or modify *
* it under the terms of the GNU Lesser General Public License as *
* published by the Free Software Foundation, either version 3 of *
* the License, or (at your option) any later version.            *
*                                                                *
* parity is distributed in the hope that it will be useful,      *
* but WITHOUT ANY WARRANTY; without even the implied warranty of *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  *
* GNU Lesser General Public License for more details.            *
*                                                                *
* You should have received a copy of the GNU Lesser General      *
* Public License along with parity. If not,                      *
* see <http://www.gnu.org/licenses/>.                            *
*                                                                *
\****************************************************************/

#include "CollectorOther.h"
#include "CollectorStubs.h"

#include "BinaryGatherer.h"

#include <Path.h>
#include <Context.h>
#include <Configuration.h>
#include <Timing.h>
#include <MappedFile.h>
#include <Log.h>
#include <Environment.h>
#include <Threading.h>

#include <CoffFileHeader.h>

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

namespace parity
{
	namespace tasks
	{
		void runConfigurationLoading()
		{
			utils::Context& context = utils::Context::getContext();
			utils::Timing::instance().start("Configuration loading");

			//
			// On Win32:
			//  1) try in current directory
			//  2) try next to exe file
			//
			// On All others (UNIX like):
			//  1) try in current directory
			//  2) try in configured PARITY_HOME/etc/
			//
			// And everywhere:
			//  3) in path set by envoironemtn variable PARITY_CONFIG
			//

		#ifdef _WIN32
			char fnBuffer[1024];
			GetModuleFileName(GetModuleHandle(NULL), fnBuffer, 1024);

			utils::Path locations[] = {
				utils::Path("."),
				utils::Path(fnBuffer)
			};
			locations[1] = locations[1].base();
		#else
			utils::Path locations[] = {
				utils::Path("."),
				utils::Path(PARITY_HOME),
			};
			locations[1].append("etc");
		#endif
			
			bool bLoaded = false;
			for(int i = 0; i < 2; ++i)
			{
				locations[i].append("parity.conf");
				locations[i].toNative();

				if(locations[i].exists())
				{
					utils::MappedFile config(locations[i], utils::ModeRead);

					try {
						utils::Config::parseFile(context, config);
					} catch(const utils::Exception& e) {
						utils::Log::error("while parsing configuration: %s\n", e.what());
						exit(1);
					}

					bLoaded = true;

					break;
				}
			}

			utils::Environment parityConfig("PARITY_CONFIG");
			utils::Path parityPath = parityConfig.getPath();
			parityPath.toNative();

			if(parityPath.exists())
			{
				utils::MappedFile config(parityPath, utils::ModeRead);

				try {
					utils::Config::parseFile(context, config);
				} catch(const utils::Exception& e) {
					utils::Log::error("while parsing configuration: %s\n", e.what());
					exit(1);
				}

				bLoaded = true;
			}

			if(!bLoaded)
			{
				utils::Log::error("cannot find any configuration. cannot continue!\n");
				exit(1);
			}

			//
			// Test for known bad configurations...
			//
			if(context.getGatherSystem() && context.getExportFromExe())
			{
				utils::Log::warning("There are known issues when gathering from system libraries, and exporting symbols from executables! Disabling export...\n");
				context.setExportFromExe(false);
			}

			utils::Timing::instance().stop("Configuration loading");
		}

		static void lookupParityRuntimeInclude()
		{
			utils::Context& ctx = utils::Context::getContext();
			utils::Log::verbose("trying to find suitable partity.runtime include directory...\n");

			if(ctx.getPCRTInclude().get().empty())
			{
				utils::Path location;

				#ifdef _WIN32
					//
					// just try the default solution setup, and parallell to executable,
					// then give up, if not found.
					//
					char fnBuffer[1024];
					GetModuleFileName(GetModuleHandle(NULL), fnBuffer, 1024);
					
					location = utils::Path(fnBuffer);
					location = location.base();
					location = location.base();	// base 2 times to get rid of the "debug" or "release" dirs.
					location.append("parity.runtime");

					if(!location.exists() || !location.isDirectory()) {
						location = utils::Path(".");
						location.append("parity.runtime");
					}
				#else
					location = utils::Path(PARITY_HOME);
					location.append("include");
					location.append("parity.runtime");
				#endif

				if(location.exists() && location.isDirectory()) {
					//
					// i know this is slow, but vector can't add at front.
					//
					utils::PathVector vec;
					vec.push_back(location);
					vec.insert(vec.end(), ctx.getSysIncludePaths().begin(), ctx.getSysIncludePaths().end());
					ctx.setSysIncludePaths(vec);
				} else
					throw utils::Exception("cannot find parity.runtime include directory!");
			} else {
				//
				// i know this is slow, but vector can't add at front.
				//
				utils::PathVector vec;
				vec.push_back(ctx.getPCRTInclude());
				vec.insert(vec.end(), ctx.getSysIncludePaths().begin(), ctx.getSysIncludePaths().end());
				ctx.setSysIncludePaths(vec);
			}
		}

		void runCompilerStage()
		{
			utils::Threading threading;
			utils::Context& context = utils::Context::getContext();

			try {
				lookupParityRuntimeInclude();
			} catch(const utils::Exception&) {
				utils::Log::error("cannot find suitable parity.runtime include directory!\n");
				exit(1);
			}
			//
			// First task is the Dependency Tracker (in background)
			// With the GNU Backend, dependency tracking is a real side
			// effect of compilation.
			//
			if(context.getDependencyTracking() && !context.getSources().empty()
				&& context.getFrontendType() == utils::ToolchainInterixGNU && context.getBackendType() == utils::ToolchainMicrosoft)
			{
				threading.run(tasks::TaskStubs::runDependencyTracking, 0, true);

				if(context.getDependencyOnly())
				{
					utils::Log::verbose("only dependency tracking requested, exiting.\n");
					threading.synchronize();
					exit(0);
				}
			}

			//
			// compiler runs parallel to depdendency tracking in the best case
			// but itself in the foreground, since we need to block on it anyway!
			//
			if(!context.getSources().empty())
			{
				//threading.run(TaskStubs::runCompiler);
				if(TaskStubs::runCompiler(0) != 0)
				{
					utils::Log::error("error executing compiler!\n");
					exit(1);
				}

				if(context.getCompileOnly())
				{
					utils::Log::verbose("only compilation requested, exiting.\n");
					threading.synchronize();
					exit(0);
				}

				if(context.getPreprocess())
				{
					utils::Log::verbose("only preprocessing requested, exiting.\n");
					threading.synchronize();
					exit(0);
				}
			}
		}

		static void lookupParityRuntimeLibrary()
		{
			utils::Context& ctx = utils::Context::getContext();
			utils::Log::verbose("trying to find suitable partity.runtime library...\n");

			if(ctx.getPCRTLibrary().get().empty())
			{
				//
				// try to lookup library ourselves
				//
				utils::Path location;

				#ifdef _WIN32
					char fnBuffer[1024];
					GetModuleFileName(GetModuleHandle(NULL), fnBuffer, 1024);
					
					location = utils::Path(fnBuffer);
					location = location.base();
					location.append("parity.runtime.lib");
				#else
					location = utils::Path(PARITY_HOME);
					location.append("lib");
					location.append("libparity_parity.runtime.a");

					if(!location.exists())
					{
						//
						// try from non-installed structure?
						//
						utils::Log::warning("TODO: non-installed runtime library search.");
					}
				#endif

				if(location.exists())
					ctx.getObjectsLibraries().push_back(location);
				else
					throw utils::Exception("cannot lookup parity.runtime library, please set in configuration!");
			} else {
				ctx.getObjectsLibraries().push_back(ctx.getPCRTLibrary());
			}
		}

		void runLinkerStage()
		{
			utils::Threading threading;
			//
			// The first part of linking is gathering all the symbols for all
			// the given libraries and objects. As a side effect this decides
			// wether to link debugable. This must be done in foreground.
			// This also cannot be swapped to a threading function that is
			// called directly (like done with some other tasks), because
			// this is the only task where data needs to be taken from after
			// processing.
			//

			utils::Timing::instance().start("Symbol gathering");

			binary::Symbol::SymbolVector exportedSymbols;
			binary::Symbol::SymbolVector staticImports;
			tasks::BinaryGatherer::ImportHybridityMap loadedImports;

			try {
				tasks::BinaryGatherer gatherer;
				gatherer.doWork();

				exportedSymbols	= gatherer.getExportedSymbols();
				staticImports	= gatherer.getStaticImports();
				loadedImports	= gatherer.getLoadedImports();

			} catch(const utils::Exception& e) {
				utils::Log::error("while gathering from binaries: %s\n", e.what());
				exit(1);
			}

			utils::Timing::instance().stop("Symbol gathering");

			//
			// The second part is generating the exports for symbol exports
			// by a DLL (if linking shared). This can be done in background.
			//
			if(!exportedSymbols.empty())
				threading.run(TaskStubs::runMsExportGenerator, &exportedSymbols, false);

			//
			// The third part is generating the import symbols for all static
			// libraries involved in linking. This can be done in Background too.
			// This should only generate symbols for things requested from
			// somewhere.
			//
			if(!staticImports.empty())
				threading.run(TaskStubs::runMsStaticImportGenerator, &staticImports, false);

			//
			// The fourth part is generating the binaries for the shared library
			// loader. This can be done in Background, since it does not require
			// any information from the exports generator and the import generator.
			//
			// This is run *always* because parts of parity.runtime depend on
			// code from parity.loader. this means that it must be present
			// always, even if it's not required.
			//
			threading.run(TaskStubs::runLoaderGenerator, &loadedImports, false);

			//
			// The last part finally is linking itself. Before doing this, all
			// background tasks must be synchronized, since the linker needs
			// all created files and informations.
			//
			threading.synchronize();

			//
			// need the parity.runtime library. for this to work, one need to set
			// the system include directories to include the parity.runtime include
			// directory as first one.
			//
			try {
				lookupParityRuntimeLibrary();
			} catch(const utils::Exception&) {
				utils::Log::error("cannot find suitable parity.runtime library!\n");
				exit(1);
			}

			if(TaskStubs::runLinker(0) != 0)
			{
				utils::Log::error("cannot run linker!\n");
				exit(1);
			}
		}
	}
}
