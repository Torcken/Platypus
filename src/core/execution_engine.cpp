#include "execution_engine.hpp"
#include "context_manager.hpp"
#include "../llm/chat_formatter.hpp"
#include "../util/logger.hpp"
#include <iostream>
#include <sstream>

namespace platypus::core {

ExecutionEngine::ExecutionEngine(std::shared_ptr<llm::LlmBackend> backend, 
                                std::shared_ptr<ToolRegistry> tool_registry,
                                size_t max_history)
    : backend_(std::move(backend)), 
      tool_registry_(std::move(tool_registry)), 
      context_manager_(std::make_unique<ContextManager>(backend, max_history)) {}

util::Result<std::string> ExecutionEngine::run(const std::string& query, int max_steps, bool headless) {
    context_manager_->add_turn("user", query);
    context_manager_->update();
    
    if (!headless) {
        util::Logger::get_instance().info("Starting execution engine loop for query: " + query);
    }

    for (int step = 0; step < max_steps; ++step) {
        std::string prompt = build_prompt(context_manager_->get_history());
        
        auto response_result = backend_->generate_chat({{"user", prompt}}, 0.3f, 512);
        if (!response_result.is_ok()) {
            return util::Result<std::string>::fail("LLM Backend error: " + response_result.error());
        }

        std::string response_content = response_result.value()[0].content;
        
        std::unique_ptr<Action> action = nullptr;
        bool plan_detected = false;
        try {
            auto json_response = nlohmann::json::parse(response_content);
            
            if (json_response.contains("plan") && json_response["plan"].is_array()) {
                plan_detected = true;
                current_plan_.clear();
                for (auto& p : json_response["plan"]) {
                    current_plan_.push_back({p["tool_name"], p["arguments"]});
                }
                active_plan_index_ = 0;
            } else if (json_response.contains("tool_call")) {
                action = std::make_unique<Action>(Action{
                    json_response["tool_call"]["name"],
                    json_response["tool_call"]["arguments"]
                });
            }
        } catch (...) {}

        if (plan_detected) {
            if (!headless) {
                util::Logger::get_instance().info("Plan detected with " + std::to_string(current_plan_.size()) + " steps.");
            }

            while (active_plan_index_ < current_plan_.size()) {
                const auto& plan_step = current_plan_[active_plan_index_];
                const Tool* tool = tool_registry_->get_tool(plan_step.tool_name);
                
                if (tool) {
                    auto tool_result = tool->execute(plan_step.arguments);
                    if (tool_result.is_ok()) {
                        std::string output = "Tool " + plan_step.tool_name + " output: " + tool_result.value();
                        context_manager_->add_turn("assistant", "Action taken (plan step " + std::to_string(active_plan_index_ + 1) + "): " + plan_step.tool_name);
                        context_manager_->add_turn("system", output);
                        context_manager_->update();
                        if (!headless) {
                            util::Logger::get_instance().debug("Plan step " + std::to_string(active_plan_index_ + 1) + " executed successfully.");
                        }
                        active_plan_index_++;
                    } else {
                        std::string error_msg = "Tool error in plan step '" + plan_step.tool_name + "': " + tool_result.error();
                        context_manager_->add_turn("system", error_msg);
                        context_manager_->update();
                        if (!headless) {
                            util::Logger::get_instance().error("Plan execution failed at step " + std::to_string(active_plan_index_ + 1) + ". Breaking while loop for re-planning.");
                        }
                        break;
                    }
                } else {
                    std::string error_msg = "Error: Tool " + plan_step.tool_name + " not found in plan.";
                    context_manager_->add_turn("system", error_msg);
                    context_manager_->update();
                    break;
                }
            }
            // All plan steps completed successfully, return a completion message
            if (active_plan_index_ == current_plan_.size()) {
                return util::Result<std::string>::ok("Plan completed successfully.");
            }

        } else if (action) {
            if (!headless) {
                util::Logger::get_instance().debug("Action detected: " + action->tool_name);
            }
            const Tool* tool = tool_registry_->get_tool(action->tool_name);
            
            if (tool) {
                auto tool_result = tool->execute(action->arguments);
                if (tool_result.is_ok()) {
                    std::string output = "Tool " + action->tool_name + " output: " + tool_result.value();
                    context_manager_->add_turn("assistant", "Action taken: " + action->tool_name);
                    context_manager_->add_turn("system", output);
                    context_manager_->update();
                    if (!headless) {
                        util::Logger::get_instance().debug("Tool executed successfully.");
                    }
                } else {
                    context_manager_->add_turn("system", "Tool error in '" + action->tool_name + "': " + tool_result.error());
                    context_manager_->update();
                    if (!headless) {
                        util::Logger::get_instance().error("Tool execution failed, reporting error to history.");
                    }
                    return util::Result<std::string>::fail("Tool execution failed: " + tool_result.error());
                }
            } else {
                context_manager_->add_turn("system", "Error: Tool " + action->tool_name + " not found.");
                context_manager_->update();
            }
        } else {
            context_manager_->add_turn("assistant", response_content);
            context_manager_->update();
            return util::Result<std::string>::ok(response_content);
        }
    }

    return util::Result<std::string>::fail("Maximum steps exceeded.");
}

std::string ExecutionEngine::build_prompt(const std::vector<std::pair<std::string, std::string>>& history) {
    std::string prompt = platypus::llm::ChatFormatter::to_json(history);
    std::string summary = context_manager_->get_summary();
    if (!summary.empty()) {
        prompt = "You are a helpful coding assistant. Use the following summary to understand the context of the ongoing task and previous decisions before processing the history below.\n\nContext Summary:\n" + summary + "\n\n" + prompt;
    }
    return prompt;
}

} // namespace platypus::core
