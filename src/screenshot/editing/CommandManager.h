#pragma once

#include <vector>
#include <memory>

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
};

class CommandManager {
public:
    CommandManager() = default;
    ~CommandManager() = default;

    void ExecuteCommand(std::unique_ptr<ICommand> command);
    void Undo();
    void Redo();
    
    bool CanUndo() const;
    bool CanRedo() const;
    void Clear();

private:
    std::vector<std::unique_ptr<ICommand>> undoStack_;
    std::vector<std::unique_ptr<ICommand>> redoStack_;
};
