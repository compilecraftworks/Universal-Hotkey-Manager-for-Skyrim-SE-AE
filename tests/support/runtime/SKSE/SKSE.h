#pragma once
#include <functional>
#include <vector>
namespace SKSE {struct TaskInterface {std::vector<std::function<void()>> tasks;void AddUITask(std::function<void()> f){tasks.push_back(f);}}; inline TaskInterface service;inline TaskInterface* GetTaskInterface(){return &service;} }
