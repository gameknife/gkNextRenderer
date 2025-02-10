#pragma once
#include <functional>
#include <unordered_map>
#include <cstdint>
#include "Utilities/Exception.hpp"

enum class EEditorCommand {
    ECmdSystem_RequestExit,
    ECmdSystem_RequestMinimize,
    ECmdSystem_RequestMaximum,

    ECmdIO_LoadScene,
    ECmdIO_LoadHDRI,
};

struct EditorCommandArgs {
    uint8_t context_1k[1024];
};

typedef std::function<bool (std::string& args)> CommandFunc;

class EditorCommand {
public:
    static void RegisterEdtiorCommand( EEditorCommand cmd, CommandFunc lambda ) {
        if( commandMaps_.find(cmd) == commandMaps_.end() ) {
            commandMaps_[cmd] = lambda;
        }
        else {
            Throw(std::runtime_error(std::string("Duplicated Command")));
        }
    }
    
    static bool ExecuteCommand( EEditorCommand cmd, std::string& args) {
        if( commandMaps_.find(cmd) != commandMaps_.end() ) {
            return commandMaps_[cmd](args);
        }
        else {
            Throw(std::runtime_error(std::string("Call Unregisterd Command")));
        }
        return false;
    }

    static bool ExecuteCommand( EEditorCommand cmd) {
        std::string args;
        return ExecuteCommand(cmd, args);
    }
private:
    static std::unordered_map<EEditorCommand, CommandFunc> commandMaps_;
};

inline std::unordered_map<EEditorCommand, CommandFunc> EditorCommand::commandMaps_;