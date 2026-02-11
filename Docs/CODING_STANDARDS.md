# Coding Standards

## Member Naming

**Style:** `m_` + PascalCase

All non-static class members use the `m_` prefix followed by PascalCase:

```cpp
class Example
{
private:
    bool m_IsRunning = true;
    std::unique_ptr<Window> m_Window;
    uint32_t m_Width;
    uint32_t m_Height;
};
```

Rationale: Consistent naming across the codebase improves readability and makes member variables immediately identifiable.

## Application Ownership

`CreateApplication()` returns `std::unique_ptr<Application>` so that `main()` can take ownership. This avoids leaks when exceptions occur during application creation or execution—the `unique_ptr` destructor guarantees cleanup.

```cpp
std::unique_ptr<Limitless::Application> CreateApplication()
{
    return std::make_unique<MyApp>();
}
```

## File and Folder Naming

- Start with capital letter
- No abbreviations (e.g., use `Source` not `src`)
