#include "CommandManager.h"

void CommandManager::ExecuteCommand(std::unique_ptr<ICommand> command) {
    if (command) {
        command->Execute();
        undoStack_.push_back(std::move(command));
        redoStack_.clear();
    }
}

void CommandManager::Undo() {
    if (CanUndo()) {
        auto command = std::move(undoStack_.back());
        undoStack_.pop_back();
        
        command->Undo();
        
        redoStack_.push_back(std::move(command));
    }
}

void CommandManager::Redo() {
    if (CanRedo()) {
        auto command = std::move(redoStack_.back());
        redoStack_.pop_back();
        
        command->Execute();
        
        undoStack_.push_back(std::move(command));
    }
}

bool CommandManager::CanUndo() const {
    return !undoStack_.empty();
}

bool CommandManager::CanRedo() const {
    return !redoStack_.empty();
}

void CommandManager::Clear() {
    undoStack_.clear();
    redoStack_.clear();
}
