# LimitlessRemaster

A modern C++ engine project with comprehensive CI/CD setup.

## Project Structure

```
LimitlessRemaster/
├── Limitless/          # Core engine library
├── Sandbox/           # Example application
├── Test/              # Unit tests using doctest
├── Scripts/           # Build scripts
├── Vendor/            # Third-party dependencies
└── .github/workflows/ # GitHub Actions CI/CD
```

## Dependencies

- **Premake5**: Build system generator
- **doctest**: Unit testing framework
- **spdlog**: Logging library
- **nlohmann/json**: JSON library

## Building

### Windows
```batch
Scripts\build-windows.bat [Debug|Release|Dist]
```

### Unix/Linux/macOS
```bash
Scripts/build-unix.sh --config Debug --compiler gcc
Scripts/build-unix.sh --config Release --compiler clang
```

### Using Premake Directly
```bash
# Generate Visual Studio solution
Vendor/Premake/premake5 vs2022

# Generate Makefiles
Vendor/Premake/premake5 gmake2

# Build with make
make -j$(nproc) config=Debug_x64
```

## Testing

The project uses [doctest](https://github.com/doctest/doctest) for unit testing.

```bash
# Run tests
./Build/Debug_x64/Test/Test --success

# Run with verbose output
./Build/Debug_x64/Test/Test --success --verbose
```

## Continuous Integration

This project includes comprehensive GitHub Actions CI/CD workflows:

### Basic CI (`ci.yml`)
- Builds on Windows, macOS, and Linux
- Tests Debug and Release configurations
- Code quality checks with clang-format and clang-tidy

### Advanced CI (`ci-advanced.yml`)
- Build caching for faster builds
- Security scanning with AddressSanitizer
- Build artifact uploads
- TODO/FIXME comment detection
- Matrix builds across all platforms

### Features
- **Multi-platform**: Windows, macOS, Linux
- **Multi-compiler**: MSVC, GCC, Clang
- **Code Quality**: Formatting, static analysis
- **Security**: Memory error detection
- **Caching**: Faster incremental builds
- **Artifacts**: Downloadable build outputs

## Code Style

The project uses `.clang-format` for consistent code formatting:

```bash
# Format all code
clang-format -i **/*.cpp **/*.h

# Check formatting
clang-format --dry-run --Werror **/*.cpp **/*.h
```

## Development Workflow

1. **Setup**: Clone the repository and ensure all dependencies are available
2. **Build**: Use the provided build scripts or premake directly
3. **Test**: Run the test suite to ensure everything works
4. **Format**: Use clang-format to maintain consistent code style
5. **Commit**: Push changes to trigger CI/CD pipelines

## CI/CD Triggers

The workflows automatically run on:
- Push to `main` or `develop` branches
- Pull requests to `main` or `develop` branches
- Manual triggers from GitHub Actions interface

## Troubleshooting

### Build Issues
- Ensure premake5 is in the correct location (`Vendor/Premake/`)
- Check that all dependencies are properly included
- Verify platform-specific configurations in premake5.lua files

### CI Issues
- Check GitHub Actions logs for specific error messages
- Verify that all required tools are available in the CI environment
- Ensure build scripts have proper permissions (Unix systems)

### Test Issues
- Review test output for specific failure reasons
- Check if tests are platform-specific
- Verify test dependencies are available

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Ensure all tests pass
5. Format your code with clang-format
6. Submit a pull request

The CI/CD pipeline will automatically validate your changes across all supported platforms.
