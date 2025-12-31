#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

// Improved logic using instance-based state
class FixedGpuTimer {
public:
    std::string currentPath;
    std::vector<size_t> stack; // Stores lengths of previous paths

    void PushFolder(const std::string& name) {
        stack.push_back(currentPath.length());
        currentPath += name;
    }

    void PopFolder() {
        if (!stack.empty()) {
            currentPath.resize(stack.back());
            stack.pop_back();
        }
    }
    
    std::string GetScopedName(const std::string& name) {
        return currentPath + name;
    }
};

class FixedScopedGpuTimer {
public:
    FixedGpuTimer* timer_;
    bool isFolder_ = false;

    FixedScopedGpuTimer(FixedGpuTimer* timer, const std::string& name, const std::string& folder) 
        : timer_(timer), isFolder_(true) 
    {
        // Start timer for 'name' (in current context)
        // Then push folder for children
        timer_->PushFolder(folder);
    }
    
    FixedScopedGpuTimer(FixedGpuTimer* timer, const std::string& name)
        : timer_(timer)
    {
        // Normal timer, just uses current context
    }

    ~FixedScopedGpuTimer() {
        if (isFolder_) {
            timer_->PopFolder();
        }
    }
};

TEST_CASE("Fixed ScopedGpuTimer Logic Success", "[GpuTimer]") {
    FixedGpuTimer timer;
    
    // Outer Scope
    {
        FixedScopedGpuTimer outer(&timer, "Outer", "Root");
        CHECK(timer.currentPath == "Root");
        
        // Inner Scope
        {
            FixedScopedGpuTimer inner(&timer, "Inner", "Child");
            CHECK(timer.currentPath == "RootChild");
        } 
        // Inner Destructor called -> PopFolder
        
        // Back in Outer
        CHECK(timer.currentPath == "Root");
    }
    // Outer Destructor called -> PopFolder
    
    CHECK(timer.currentPath == "");
}