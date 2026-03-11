#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 /path/to/project /path/to/repo" >&2
    exit 1
fi

PROJECT_ROOT="$1"
REPO_ROOT="$2"
PROJECT_ROOT="$(cd "$PROJECT_ROOT" && pwd)"
REPO_ROOT="$(cd "$REPO_ROOT" && pwd)"
ASSETS_DIR="$PROJECT_ROOT/Assets"

if [[ ! -d "$ASSETS_DIR" ]]; then
    exit 0
fi

xml_escape() {
    local value="$1"
    value="${value//&/&amp;}"
    value="${value//</&lt;}"
    value="${value//>/&gt;}"
    value="${value//\"/&quot;}"
    printf '%s' "$value"
}

PROJECT_MANAGED_SCRIPTS=()
while IFS= read -r script_path; do
    [[ -z "$script_path" ]] && continue
    PROJECT_MANAGED_SCRIPTS+=("$script_path")
done < <(find "$ASSETS_DIR" -type f -name '*.cs' -print | LC_ALL=C sort)

if [[ ${#PROJECT_MANAGED_SCRIPTS[@]} -eq 0 ]]; then
    exit 0
fi

PROJECT_LEAF="$(basename "$PROJECT_ROOT")"
SANITIZED_PROJECT_NAME="$(printf '%s' "$PROJECT_LEAF" | tr -cd '[:alnum:]_')"
if [[ -z "$SANITIZED_PROJECT_NAME" ]]; then
    SANITIZED_PROJECT_NAME="Project"
fi
if [[ "$SANITIZED_PROJECT_NAME" =~ ^[0-9] ]]; then
    SANITIZED_PROJECT_NAME="Project_${SANITIZED_PROJECT_NAME}"
fi

MANAGED_PROJECT_ASSEMBLY_NAME="${SANITIZED_PROJECT_NAME}.ManagedScripts"
MANAGED_PROJECT_ASSEMBLY_FILE="${MANAGED_PROJECT_ASSEMBLY_NAME}.dll"
GENERATED_MANAGED_DIR="$PROJECT_ROOT/Build/Generated/ManagedScripts"
mkdir -p "$GENERATED_MANAGED_DIR"
MANAGED_PROJECT_CSPROJ="$GENERATED_MANAGED_DIR/${MANAGED_PROJECT_ASSEMBLY_NAME}.csproj"
CONTRACT_CSPROJ="$REPO_ROOT/Managed/Limitless.Managed/Limitless.Managed.csproj"

temp_csproj="$(mktemp)"
{
    echo '<Project Sdk="Microsoft.NET.Sdk">'
    echo '  <PropertyGroup>'
    echo '    <TargetFramework>net9.0</TargetFramework>'
    echo '    <OutputType>Library</OutputType>'
    echo '    <Nullable>enable</Nullable>'
    echo '    <ImplicitUsings>enable</ImplicitUsings>'
    echo '    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>'
    printf '    <AssemblyName>%s</AssemblyName>\n' "$(xml_escape "$MANAGED_PROJECT_ASSEMBLY_NAME")"
    printf '    <RootNamespace>%s</RootNamespace>\n' "$(xml_escape "$MANAGED_PROJECT_ASSEMBLY_NAME")"
    echo '    <EnableDynamicLoading>true</EnableDynamicLoading>'
    echo '  </PropertyGroup>'
    echo '  <ItemGroup Condition="''$(LimitlessManagedReferencePath)'' != ''''">'
    echo '    <Reference Include="Limitless.Managed">'
    echo '      <HintPath>$(LimitlessManagedReferencePath)</HintPath>'
    echo '      <Private>true</Private>'
    echo '    </Reference>'
    echo '  </ItemGroup>'
    echo '  <ItemGroup Condition="''$(LimitlessManagedReferencePath)'' == ''''">'
    printf '    <ProjectReference Include="%s" />\n' "$(xml_escape "$CONTRACT_CSPROJ")"
    echo '  </ItemGroup>'
    echo '  <ItemGroup>'
    for script_path in "${PROJECT_MANAGED_SCRIPTS[@]}"; do
        relative_path="${script_path#"$ASSETS_DIR"/}"
        relative_path="${relative_path//\\//}"
        printf '    <Compile Include="%s"><Link>%s</Link></Compile>\n' \
            "$(xml_escape "$script_path")" \
            "$(xml_escape "Assets/$relative_path")"
    done
    echo '  </ItemGroup>'
    echo '</Project>'
} > "$temp_csproj"

if [[ -f "$MANAGED_PROJECT_CSPROJ" ]] && cmp -s "$temp_csproj" "$MANAGED_PROJECT_CSPROJ"; then
    rm -f "$temp_csproj"
else
    mv "$temp_csproj" "$MANAGED_PROJECT_CSPROJ"
fi

printf '%s|%s\n' "$MANAGED_PROJECT_CSPROJ" "$MANAGED_PROJECT_ASSEMBLY_FILE"
