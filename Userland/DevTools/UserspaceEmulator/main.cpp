/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Emulator.h"
#include <AK/FileStream.h>
#include <AK/Format.h>
#include <AK/LexicalPath.h>
#include <AK/StringBuilder.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/DirIterator.h>
#include <LibCore/File.h>
#include <fcntl.h>
#include <pthread.h>
#include <serenity.h>
#include <string.h>

bool g_report_to_debug = false;
bool g_dump_profile = false;
unsigned g_profile_instruction_interval = 0;
Optional<OutputFileStream> g_profile_stream;

int main(int argc, char** argv, char** env)
{
    Vector<String> arguments;
    bool pause_on_startup { false };
    String profile_dump_path;
    FILE* profile_output_file { nullptr };

    Core::ArgsParser parser;
    parser.set_stop_on_first_non_option(true);
    parser.add_option(g_report_to_debug, "Write reports to the debug log", "report-to-debug", 0);
    parser.add_option(pause_on_startup, "Pause on startup", "pause", 'p');
    parser.add_option(g_dump_profile, "Generate a ProfileViewer-compatible profile", "profile", 0);
    parser.add_option(g_profile_instruction_interval, "Set the profile instruction capture interval, 128 by default", "profile-interval", 'i', "#instructions");
    parser.add_option(profile_dump_path, "File path for profile dump", "profile-file", 0, "path");

    parser.add_positional_argument(arguments, "Command to emulate", "command");

    parser.parse(argc, argv);

    if (g_dump_profile && g_profile_instruction_interval == 0)
        g_profile_instruction_interval = 128;

    String executable_path;
    if (arguments[0].contains("/"sv))
        executable_path = Core::File::real_path_for(arguments[0]);
    else
        executable_path = Core::find_executable_in_path(arguments[0]);
    if (executable_path.is_empty()) {
        reportln("Cannot find executable for '{}'.", arguments[0]);
        return 1;
    }

    if (g_dump_profile && profile_dump_path.is_empty())
        profile_dump_path = String::formatted("{}.{}.profile", executable_path, getpid());

    if (g_dump_profile) {
        profile_output_file = fopen(profile_dump_path.characters(), "w+");
        if (profile_output_file == nullptr) {
            auto error_string = strerror(errno);
            warnln("Failed to open '{}' for writing: {}", profile_dump_path, error_string);
            return 1;
        }
        g_profile_stream = OutputFileStream { profile_output_file };

        g_profile_stream->write_or_error(R"({"events":[)"sv.bytes());
        timeval tv {};
        gettimeofday(&tv, nullptr);
        g_profile_stream->write_or_error(
            String::formatted(
                R"~({{"type": "process_create", "parent_pid": 1, "executable": "{}", "pid": {}, "tid": {}, "timestamp": {}, "lost_samples": 0, "stack": []}})~",
                executable_path, getpid(), gettid(), tv.tv_sec * 1000 + tv.tv_usec / 1000)
                .bytes());
    }

    Vector<String> environment;
    for (int i = 0; env[i]; ++i) {
        environment.append(env[i]);
    }

    // FIXME: It might be nice to tear down the emulator properly.
    auto& emulator = *new UserspaceEmulator::Emulator(executable_path, arguments, environment);
    if (!emulator.load_elf())
        return 1;

    StringBuilder builder;
    builder.append("(UE) ");
    builder.append(LexicalPath::basename(arguments[0]));
    if (set_process_name(builder.string_view().characters_without_null_termination(), builder.string_view().length()) < 0) {
        perror("set_process_name");
        return 1;
    }
    int rc = pthread_setname_np(pthread_self(), builder.to_string().characters());
    if (rc != 0) {
        reportln("pthread_setname_np: {}", strerror(rc));
        return 1;
    }

    if (pause_on_startup)
        emulator.pause();

    rc = emulator.exec();

    if (g_dump_profile) {
        g_profile_stream->write_or_error(R"(]})"sv.bytes());
    }
    return rc;
}
