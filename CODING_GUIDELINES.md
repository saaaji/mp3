# ESP32 RTOS Coding Guidelines

## Memory Management Rules

### 1. Avoid Dynamic Memory Allocation
- ❌ Don't use heap allocation when possible
- ✅ Prefer stack-based allocation
- ❌ Avoid `malloc`/`free` operations
- ✅ Use static arrays with fixed sizes

```cpp
// BAD
char* buffer = (char*)malloc(100);

// GOOD
char buffer[100];
```

### 2. String Handling
- ❌ Avoid `std::string` (uses dynamic memory)
- ✅ Use `std::string_view` for string references
- ✅ Use fixed-size character arrays when necessary

```cpp
// BAD
std::string dynamicString = "hello";

// GOOD
std::string_view stringView = "hello";
// or
char fixedString[6] = "hello";
```

### 3. Container Usage
- ❌ Avoid STL containers that use dynamic allocation
- ✅ Use fixed-size arrays or static containers
- ❌ Don't use `std::vector`, `std::list`, etc.

```cpp
// BAD
std::vector<int> dynamicArray;

// GOOD
std::array<int, 10> fixedArray;
```

## RTOS Best Practices

### 1. Task Management
- ✅ Always specify stack sizes explicitly
- ✅ Use static task creation whenever possible
- ✅ Set appropriate task priorities
- ❌ Don't create tasks dynamically during runtime

```cpp
// GOOD
StaticTask_t xTaskBuffer;
StackType_t xStack[1000];
xTaskCreateStatic(taskFunction, "TaskName", 1000, NULL, 1, xStack, &xTaskBuffer);
```

### 2. Critical Sections
- ✅ Keep critical sections as short as possible
- ✅ Always use proper synchronization mechanisms
- ❌ Don't block indefinitely in critical sections

### 3. Memory Protection
- ✅ Use `configASSERT` for pointer validation
- ✅ Check return values from memory operations
- ❌ Don't access freed memory

## C++ Specific Guidelines

### 1. Memory Management
- ❌ Avoid `new` and `delete` operators
- ✅ Use RAII principles
- ✅ If `new` is necessary, ensure matching `delete`

```cpp
// BAD
auto* ptr = new int(42);
// ... code ...
delete ptr;

// GOOD
int value = 42;
```

### 2. Array Initialization
- ❌ Avoid single-line complex initializations
- ✅ Use clear, readable initialization syntax

```cpp
// BAD
int arr[] = {0,1};

// GOOD
int arr[2];
arr[0] = 0;
arr[1] = 1;
```

## FreeRTOS Usage

### 1. Queue Management
- ✅ Use static queue creation
- ✅ Check queue operations for success
- ❌ Don't overflow queues

### 2. Semaphores
- ✅ Use binary semaphores for synchronization
- ✅ Use counting semaphores for resource management
- ❌ Don't use semaphores for data protection (use mutexes)

### 3. Task Communication
- ✅ Prefer task notifications over semaphores when possible
- ✅ Use queues for data transfer between tasks
- ❌ Don't share variables between tasks without protection

## Code Style

### 1. General
- ✅ Use clear, descriptive names
- ✅ Comment complex algorithms
- ✅ Keep functions small and focused
- ❌ Don't use global variables

### 2. Error Handling
- ✅ Check return values
- ✅ Use error codes consistently
- ✅ Log errors appropriately
- ❌ Don't ignore error conditions

## C++ Namespace and Scope Guidelines

### 1. Namespace Usage
- ✅ Use explicit namespace qualification (`std::string_view` instead of just `string_view`)
- ❌ Avoid `using namespace` directives in headers
- ✅ Group related functionality in namespaces

```cpp
// BAD
using namespace std;
string name;

// GOOD
std::string_view name;  // Explicit namespace qualification
```

### 2. Class Member Organization
- ✅ Use `static constexpr` for compile-time constants
- ✅ Use fixed-size containers for class members
- ✅ Initialize members at declaration when possible

```cpp
// GOOD (from Component class)
class Component {
    static constexpr std::size_t kMaxComponentNameLength = 32;  // Compile-time constant
    std::array<char, kMaxComponentNameLength> name_{'\0'};      // Fixed-size array
    StaticSemaphore_t join_sem_buffer_{};                      // Static allocation
};
```

## Testing
- ✅ Write unit tests for components
- ✅ Test error conditions
- ✅ Validate memory usage
- ❌ Don't skip error handling tests
