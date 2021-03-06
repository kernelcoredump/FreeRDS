/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2004-2012
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * main program
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>

#include "freerds.h"

#include <freerds/icp.h>

#include <winpr/crt.h>
#include <winpr/path.h>
#include <winpr/synch.h>
#include <winpr/thread.h>
#include <winpr/cmdline.h>

#include <freerds/icp_client_stubs.h>

char* RdsModuleName = NULL;
static HANDLE g_TermEvent = NULL;
static xrdpListener* g_listen = NULL;

COMMAND_LINE_ARGUMENT_A freerds_args[] =
{
	{ "kill", COMMAND_LINE_VALUE_FLAG, "", NULL, NULL, -1, NULL, "kill daemon" },
	{ "nodaemon", COMMAND_LINE_VALUE_FLAG, "", NULL, NULL, -1, NULL, "no daemon" },
	{ "module", COMMAND_LINE_VALUE_REQUIRED, "<module name>", NULL, NULL, -1, NULL, "module name" },
	{ NULL, 0, NULL, NULL, NULL, -1, NULL, NULL }
};

void freerds_shutdown(int sig)
{
	DWORD threadId;

	threadId = GetCurrentThreadId();

	printf("shutting down\n");
	printf("signal %d threadId %d\n", sig, (int) threadId);

	if (WaitForSingleObject(g_TermEvent, 0) != WAIT_OBJECT_0)
		SetEvent(g_TermEvent);
}

int g_is_term(void)
{
	return (WaitForSingleObject(g_TermEvent, 0) == WAIT_OBJECT_0) ? 1 : 0;
}

void g_set_term(int in_val)
{
	if (in_val)
		SetEvent(g_TermEvent);
	else
		ResetEvent(g_TermEvent);
}

HANDLE g_get_term_event(void)
{
	return g_TermEvent;
}

void pipe_sig(int sig_num)
{
	printf("FreeRDS SIGPIPE (%d)\n", sig_num);
}

int main(int argc, char** argv)
{
	int pid;
	FILE* fd;
	int status;
	DWORD flags;
	int no_daemon;
	int kill_process;
	char text[256];
	char pid_file[256];
	COMMAND_LINE_ARGUMENT_A* arg;

	no_daemon = kill_process = 0;

	flags = COMMAND_LINE_SEPARATOR_SPACE;
	flags |= COMMAND_LINE_SIGIL_DASH | COMMAND_LINE_SIGIL_DOUBLE_DASH;

	status = CommandLineParseArgumentsA(argc, (const char**) argv,
			freerds_args, flags, NULL, NULL, NULL);

	arg = freerds_args;

	do
	{
		if (!(arg->Flags & COMMAND_LINE_VALUE_PRESENT))
			continue;

		CommandLineSwitchStart(arg)

		CommandLineSwitchCase(arg, "kill")
		{
			kill_process = 1;
		}
		CommandLineSwitchCase(arg, "nodaemon")
		{
			no_daemon = 1;
		}
		CommandLineSwitchCase(arg, "module")
		{
			RdsModuleName = _strdup(arg->Value);
		}

		CommandLineSwitchEnd(arg)
	}
	while ((arg = CommandLineFindNextArgumentA(arg)) != NULL);

	sprintf_s(pid_file, 255, "%s/freerds.pid", FREERDS_PID_PATH);

	if (kill_process)
	{
		printf("stopping FreeRDS\n");

		fd = NULL;

		if (PathFileExistsA(pid_file))
		{
			fd = fopen(pid_file, "r");
		}

		if (!fd)
		{
			printf("problem opening freerds.pid [%s]\n", pid_file);
			printf("maybe its not running\n");
		}
		else
		{
			ZeroMemory(text, 32);
			fread((void*) text, 31, 1, fd);
			pid = atoi(text);
			printf("stopping process id %d\n", pid);

			if (pid > 0)
			{
				kill(pid, SIGTERM);
			}

			fclose(fd);
		}

		return 0;
	}

	if (PathFileExistsA(pid_file))
	{
		printf("It looks like FreeRDS is already running,\n");
		printf("if not delete the freerds.pid file and try again\n");
		return 0;
	}

	if (!no_daemon)
	{
		if (!PathFileExistsA(FREERDS_VAR_PATH))
			CreateDirectoryA(FREERDS_VAR_PATH, NULL);

		if (!PathFileExistsA(FREERDS_PID_PATH))
			CreateDirectoryA(FREERDS_PID_PATH, NULL);

		/* make sure we can write to pid file */
		fd = fopen(pid_file, "w+");

		if (!fd)
		{
			printf("running in daemon mode with no access to pid files, quitting\n");
			return 0;
		}

		if (fwrite((void*) "0", 1, 1, fd) == -1)
		{
			printf("running in daemon mode with no access to pid files, quitting\n");
			return 0;
		}

		fclose(fd);
		DeleteFileA(pid_file);
	}

	if (!no_daemon)
	{
		/* start of daemonizing code */
		pid = fork();

		if (pid == -1)
		{
			printf("problem forking\n");
			return 1;
		}

		if (0 != pid)
		{
			printf("process %d started\n", pid);
			return 0;
		}

		Sleep(1000);

		/* write the pid to file */
		pid = GetCurrentProcessId();
		fd = fopen(pid_file, "w+");

		if (!fd)
		{
			printf("trying to write process id to freerds.pid\n");
			printf("problem opening freerds.pid\n");
		}
		else
		{
			sprintf_s(text, sizeof(text), "%d", pid);
			fwrite((void*) text, strlen(text), 1, fd);
			fclose(fd);
		}

		Sleep(1000);
		close(0);
		close(1);
		close(2);
		open("/dev/null", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
		open("/dev/null", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
		open("/dev/null", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
		/* end of daemonizing code */
	}

	g_listen = freerds_listener_create();

	signal(SIGINT, freerds_shutdown);
	signal(SIGKILL, freerds_shutdown);
	signal(SIGPIPE, pipe_sig);
	signal(SIGPIPE, freerds_shutdown);

	pid = GetCurrentProcessId();

	g_TermEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	printf("starting icp and waiting for session manager \n");
	freerds_icp_start();
	printf("connected to session manager\n");

	freerds_listener_main_loop(g_listen);
	freerds_listener_delete(g_listen);

	CloseHandle(g_TermEvent);

	/* only main process should delete pid file */
	if ((!no_daemon) && (pid == GetCurrentProcessId()))
	{
		DeleteFileA(pid_file);
	}

	freerds_icp_shutdown();

	return 0;
}
