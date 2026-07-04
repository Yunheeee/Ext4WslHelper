#pragma once
#include "../src/command/ICommandExecutor.h"
#include <vector>
#include <string>
#include <algorithm>

class MockCommandExecutor : public ICommandExecutor {
public:
    struct Expectation {
        std::string commandPrefix;
        CommandResult result;
    };

    struct ExecutedCommand {
        std::string commandLine;
        std::string stdinInput;
    };

    void AddExpectation(const std::string& commandPrefix, CommandResult result) {
        expectations_.push_back({ commandPrefix, result });
    }

    void AddExactExpectation(const std::string& commandLine, CommandResult result) {
        exactExpectations_.push_back({ commandLine, result });
    }

    CommandResult Execute(const std::string& commandLine,
                          const std::string& stdinInput = "",
                          DWORD timeoutMs = 30000) override {
        executedCommands_.push_back({ commandLine, stdinInput });

        // Check exact match first
        for (const auto& exp : exactExpectations_) {
            if (commandLine == exp.commandPrefix) {
                return exp.result;
            }
        }

        // Then prefix match
        for (const auto& exp : expectations_) {
            if (commandLine.find(exp.commandPrefix) != std::string::npos) {
                return exp.result;
            }
        }

        return { (DWORD)-1, "", "Unexpected command: " + commandLine };
    }

    HANDLE StartBackground(const std::string& commandLine) override {
        return INVALID_HANDLE_VALUE;
    }

    bool IsAvailable() override { return true; }

    const std::vector<ExecutedCommand>& GetExecutedCommands() const {
        return executedCommands_;
    }

    void ClearHistory() {
        executedCommands_.clear();
        expectations_.clear();
        exactExpectations_.clear();
    }

private:
    std::vector<Expectation> expectations_;
    std::vector<Expectation> exactExpectations_;
    std::vector<ExecutedCommand> executedCommands_;
};
