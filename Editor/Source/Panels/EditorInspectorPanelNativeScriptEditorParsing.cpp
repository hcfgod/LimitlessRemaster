#include "PrecompiledHeader.h"
#include "EditorInspectorPanelNativeScriptEditorShared.h"

#include "Assets/AssetDatabase.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace Limitless::EditorInspectorPanel::Internal
{

        struct ScriptPublicFieldDefinition final
        {
            std::string Name;
            ScriptPropertyValue DefaultValue;
        };

        std::string TrimString(const std::string& value)
        {
            size_t start = 0;
            while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
                ++start;

            size_t end = value.size();
            while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
                --end;

            return value.substr(start, end - start);
        }

        bool TryParseFloatLiteral(const std::string& rawValue, float& outValue)
        {
            std::string value = TrimString(rawValue);
            if (!value.empty() && (value.back() == 'f' || value.back() == 'F'))
                value.pop_back();
            if (value.empty())
                return false;

            char* parseEnd = nullptr;
            const float parsedValue = std::strtof(value.c_str(), &parseEnd);
            if (parseEnd == value.c_str() || *parseEnd != '\0')
                return false;
            outValue = parsedValue;
            return true;
        }

        bool TryParseIntegerLiteral(const std::string& rawValue, int32_t& outValue)
        {
            const std::string value = TrimString(rawValue);
            if (value.empty())
                return false;

            char* parseEnd = nullptr;
            const long parsedValue = std::strtol(value.c_str(), &parseEnd, 10);
            if (parseEnd == value.c_str() || *parseEnd != '\0')
                return false;
            outValue = static_cast<int32_t>(parsedValue);
            return true;
        }

        bool TryParseVector3Literal(const std::string& rawValue, glm::vec3& outValue)
        {
            const std::regex numberPattern(R"([+-]?\d*\.?\d+(?:[eE][+-]?\d+)?)");
            std::vector<float> values;
            for (std::sregex_iterator iterator(rawValue.begin(), rawValue.end(), numberPattern), end; iterator != end; ++iterator)
            {
                float parsedValue = 0.0f;
                if (!TryParseFloatLiteral(iterator->str(), parsedValue))
                    continue;
                values.push_back(parsedValue);
                if (values.size() == 3)
                    break;
            }

            if (values.size() != 3)
                return false;

            outValue = glm::vec3(values[0], values[1], values[2]);
            return true;
        }

        bool TryParseVector2Literal(const std::string& rawValue, glm::vec2& outValue)
        {
            const std::regex numberPattern(R"([+-]?\d*\.?\d+(?:[eE][+-]?\d+)?)");
            std::vector<float> values;
            for (std::sregex_iterator iterator(rawValue.begin(), rawValue.end(), numberPattern), end; iterator != end; ++iterator)
            {
                float parsedValue = 0.0f;
                if (!TryParseFloatLiteral(iterator->str(), parsedValue))
                    continue;
                values.push_back(parsedValue);
                if (values.size() == 2)
                    break;
            }

            if (values.size() != 2)
                return false;

            outValue = glm::vec2(values[0], values[1]);
            return true;
        }

        bool TryParseVector4Literal(const std::string& rawValue, glm::vec4& outValue)
        {
            const std::regex numberPattern(R"([+-]?\d*\.?\d+(?:[eE][+-]?\d+)?)");
            std::vector<float> values;
            for (std::sregex_iterator iterator(rawValue.begin(), rawValue.end(), numberPattern), end; iterator != end; ++iterator)
            {
                float parsedValue = 0.0f;
                if (!TryParseFloatLiteral(iterator->str(), parsedValue))
                    continue;
                values.push_back(parsedValue);
                if (values.size() == 4)
                    break;
            }

            if (values.size() != 4)
                return false;

            outValue = glm::vec4(values[0], values[1], values[2], values[3]);
            return true;
        }

        bool TryBuildDefaultFieldValue(const std::string& typeName,
                                       const std::optional<std::string>& rawInitializer,
                                       ScriptPropertyValue& outValue)
        {
            const std::string initializer = rawInitializer.has_value() ? TrimString(rawInitializer.value()) : std::string();

            if (typeName == "float")
            {
                float value = 0.0f;
                if (!initializer.empty() && !TryParseFloatLiteral(initializer, value))
                    return false;
                outValue = value;
                return true;
            }
            if (typeName == "int" || typeName == "int32_t")
            {
                int32_t value = 0;
                if (!initializer.empty() && !TryParseIntegerLiteral(initializer, value))
                    return false;
                outValue = value;
                return true;
            }
            if (typeName == "bool")
            {
                bool value = false;
                if (!initializer.empty())
                {
                    if (initializer == "true")
                        value = true;
                    else if (initializer == "false")
                        value = false;
                    else
                        return false;
                }
                outValue = value;
                return true;
            }
            if (typeName == "glm::vec2")
            {
                glm::vec2 value(0.0f);
                if (!initializer.empty() && !TryParseVector2Literal(initializer, value))
                    return false;
                outValue = value;
                return true;
            }
            if (typeName == "glm::vec3")
            {
                glm::vec3 value(0.0f);
                if (!initializer.empty() && !TryParseVector3Literal(initializer, value))
                    return false;
                outValue = value;
                return true;
            }
            if (typeName == "glm::vec4")
            {
                glm::vec4 value(0.0f);
                if (!initializer.empty() && !TryParseVector4Literal(initializer, value))
                    return false;
                outValue = value;
                return true;
            }
            if (typeName == "std::string")
            {
                std::string value;
                if (!initializer.empty())
                {
                    if (initializer.size() < 2 || initializer.front() != '"' || initializer.back() != '"')
                        return false;
                    value = initializer.substr(1, initializer.size() - 2);
                }
                outValue = value;
                return true;
            }
            if (typeName == "Limitless::Entity" || typeName == "Entity")
            {
                ScriptEntityReference value{};
                if (!initializer.empty())
                {
                    if (initializer == "{}" ||
                        initializer == "Entity{}" ||
                        initializer == "Limitless::Entity{}" ||
                        initializer == "Entity()" ||
                        initializer == "Limitless::Entity()")
                    {
                        value.Tag.clear();
                    }
                    else if (initializer.size() >= 2 && initializer.front() == '"' && initializer.back() == '"')
                    {
                        const std::string parsedValue = initializer.substr(1, initializer.size() - 2);
                        if (LooksLikePrefabAssetKey(parsedValue))
                            value.PrefabAssetKey = parsedValue;
                        else
                            value.Tag = parsedValue;
                    }
                    else
                    {
                        return false;
                    }
                }
                outValue = value;
                return true;
            }
            if (typeName == "Limitless::Prefab" ||
                typeName == "Prefab" ||
                typeName == "Limitless::ScriptPrefabReference" ||
                typeName == "ScriptPrefabReference")
            {
                Prefab value{};
                if (!initializer.empty())
                {
                    if (initializer == "{}" ||
                        initializer == "Prefab{}" ||
                        initializer == "Limitless::Prefab{}" ||
                        initializer == "Prefab()" ||
                        initializer == "Limitless::Prefab()" ||
                        initializer == "ScriptPrefabReference{}" ||
                        initializer == "Limitless::ScriptPrefabReference{}" ||
                        initializer == "ScriptPrefabReference()" ||
                        initializer == "Limitless::ScriptPrefabReference()")
                    {
                        value.AssetKey.clear();
                    }
                    else if (initializer.size() >= 2 && initializer.front() == '"' && initializer.back() == '"')
                    {
                        value.AssetKey = initializer.substr(1, initializer.size() - 2);
                    }
                    else
                    {
                        return false;
                    }
                }
                outValue = std::move(value);
                return true;
            }

            return false;
        }

        struct ParsedEnumDefinition final
        {
            std::string Name;
            std::vector<std::string> Values;
        };

        void ParseEnumDefinitionsFromSource(const std::string& source, std::vector<ParsedEnumDefinition>& outEnums)
        {
            const std::regex enumDeclPattern(R"(enum\s+(?:class\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*[A-Za-z_][A-Za-z0-9_:]*)?\s*\{([^}]*)\})");
            const std::regex enumValuePattern(R"(([A-Za-z_][A-Za-z0-9_]*))");

            for (std::sregex_iterator it(source.begin(), source.end(), enumDeclPattern), end; it != end; ++it)
            {
                ParsedEnumDefinition enumDef;
                enumDef.Name = (*it)[1].str();

                const std::string body = (*it)[2].str();
                for (std::sregex_iterator vit(body.begin(), body.end(), enumValuePattern), vend; vit != vend; ++vit)
                {
                    enumDef.Values.push_back((*vit)[1].str());
                }
                if (!enumDef.Values.empty())
                    outEnums.push_back(std::move(enumDef));
            }
        }

        const ParsedEnumDefinition* FindEnumDefinition(const std::vector<ParsedEnumDefinition>& enums, const std::string& typeName)
        {
            for (const auto& enumDef : enums)
            {
                if (enumDef.Name == typeName)
                    return &enumDef;
            }
            return nullptr;
        }

        bool ParsePublicScriptFieldsFromHeader(const std::filesystem::path& headerPath,
                                               std::vector<ScriptPublicFieldDefinition>& outFields,
                                               std::string& outError)
        {
            std::ifstream input(headerPath, std::ios::in | std::ios::binary);
            if (!input.is_open())
            {
                outError = "Failed to open script header: " + headerPath.string();
                return false;
            }

            const std::string fileContent((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            input.close();

            std::vector<ParsedEnumDefinition> discoveredEnums;
            ParseEnumDefinitionsFromSource(fileContent, discoveredEnums);

            std::string enumTypeAlternation;
            for (const auto& enumDef : discoveredEnums)
            {
                if (!enumTypeAlternation.empty())
                    enumTypeAlternation += "|";
                enumTypeAlternation += enumDef.Name;
            }

            const std::string baseTypes = "float|int32_t|int|bool|glm::vec2|glm::vec3|glm::vec4|std::string|Limitless::Entity|Entity|Limitless::Prefab|Prefab|Limitless::ScriptPrefabReference|ScriptPrefabReference";
            const std::string fullPattern = enumTypeAlternation.empty()
                ? std::string(R"(^\s*(?:const\s+)?(?:static\s+)?()" + baseTypes + R"()\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*([^;]+))?\s*;\s*$)")
                : std::string(R"(^\s*(?:const\s+)?(?:static\s+)?()" + baseTypes + "|" + enumTypeAlternation + R"()\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*([^;]+))?\s*;\s*$)");

            const std::regex fieldPattern(fullPattern);

            bool insidePublicSection = false;
            std::istringstream stream(fileContent);
            std::string line;
            while (std::getline(stream, line))
            {
                const size_t commentIndex = line.find("//");
                const std::string content = TrimString(commentIndex == std::string::npos ? line : line.substr(0, commentIndex));
                if (content.empty())
                    continue;

                if (content == "public:")
                {
                    insidePublicSection = true;
                    continue;
                }
                if (content == "private:" || content == "protected:")
                {
                    insidePublicSection = false;
                    continue;
                }

                if (!insidePublicSection)
                    continue;
                if (content.find('(') != std::string::npos)
                    continue;

                std::smatch fieldMatch;
                if (!std::regex_match(content, fieldMatch, fieldPattern))
                    continue;

                const std::string typeName = fieldMatch[1].str();
                ScriptPublicFieldDefinition fieldDefinition;
                fieldDefinition.Name = fieldMatch[2].str();

                std::optional<std::string> initializer;
                if (fieldMatch[3].matched)
                    initializer = fieldMatch[3].str();

                const ParsedEnumDefinition* enumDef = FindEnumDefinition(discoveredEnums, typeName);
                if (enumDef != nullptr)
                {
                    ScriptEnumValue enumValue{};
                    enumValue.EnumTypeName = enumDef->Name;
                    enumValue.EnumNames = enumDef->Values;
                    enumValue.Value = 0;
                    if (initializer.has_value())
                    {
                        const std::string initTrimmed = TrimString(initializer.value());
                        for (size_t i = 0; i < enumDef->Values.size(); ++i)
                        {
                            if (initTrimmed == enumDef->Values[i] ||
                                initTrimmed == enumDef->Name + "::" + enumDef->Values[i])
                            {
                                enumValue.Value = static_cast<int32_t>(i);
                                break;
                            }
                        }
                    }
                    fieldDefinition.DefaultValue = std::move(enumValue);
                }
                else if (!TryBuildDefaultFieldValue(typeName, initializer, fieldDefinition.DefaultValue))
                {
                    continue;
                }

                outFields.push_back(std::move(fieldDefinition));
            }

            outError.clear();
            return true;
        }

        bool ParseLegacyExposedFieldsFromSource(const std::filesystem::path& sourcePath,
                                                std::vector<ScriptPublicFieldDefinition>& outFields,
                                                std::string& outError)
        {
            std::ifstream input(sourcePath, std::ios::in | std::ios::binary);
            if (!input.is_open())
            {
                outError = "Failed to open script source: " + sourcePath.string();
                return false;
            }

            const std::regex callPattern(
                R"LT(GetExposed(Float|Integer|Boolean|Vector3|String|Entity|Prefab)\s*\(\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*([^)]+)\))LT");

            std::unordered_set<std::string> existingNames;
            for (const auto& existingField : outFields)
                existingNames.insert(existingField.Name);

            std::string line;
            while (std::getline(input, line))
            {
                std::smatch callMatch;
                if (!std::regex_search(line, callMatch, callPattern))
                    continue;

                const std::string functionSuffix = callMatch[1].str();
                const std::string propertyName = callMatch[2].str();
                const std::string fallbackExpression = TrimString(callMatch[3].str());

                if (existingNames.find(propertyName) != existingNames.end())
                    continue;

                ScriptPublicFieldDefinition fieldDefinition;
                fieldDefinition.Name = propertyName;

                bool parsed = false;
                if (functionSuffix == "Float")
                {
                    float value = 0.0f;
                    parsed = TryParseFloatLiteral(fallbackExpression, value);
                    if (parsed)
                        fieldDefinition.DefaultValue = value;
                }
                else if (functionSuffix == "Integer")
                {
                    int32_t value = 0;
                    parsed = TryParseIntegerLiteral(fallbackExpression, value);
                    if (parsed)
                        fieldDefinition.DefaultValue = value;
                }
                else if (functionSuffix == "Boolean")
                {
                    if (fallbackExpression == "true")
                    {
                        fieldDefinition.DefaultValue = true;
                        parsed = true;
                    }
                    else if (fallbackExpression == "false")
                    {
                        fieldDefinition.DefaultValue = false;
                        parsed = true;
                    }
                }
                else if (functionSuffix == "Vector3")
                {
                    glm::vec3 value(0.0f);
                    parsed = TryParseVector3Literal(fallbackExpression, value);
                    if (parsed)
                        fieldDefinition.DefaultValue = value;
                }
                else if (functionSuffix == "String")
                {
                    if (fallbackExpression.size() >= 2 && fallbackExpression.front() == '"' && fallbackExpression.back() == '"')
                    {
                        fieldDefinition.DefaultValue = fallbackExpression.substr(1, fallbackExpression.size() - 2);
                        parsed = true;
                    }
                }
                else if (functionSuffix == "Entity")
                {
                    ScriptEntityReference value{};
                    if (fallbackExpression == "{}" || fallbackExpression == "Entity{}" || fallbackExpression == "Limitless::Entity{}")
                    {
                        parsed = true;
                    }
                    else if (fallbackExpression.size() >= 2 && fallbackExpression.front() == '"' && fallbackExpression.back() == '"')
                    {
                        const std::string parsedValue = fallbackExpression.substr(1, fallbackExpression.size() - 2);
                        if (LooksLikePrefabAssetKey(parsedValue))
                            value.PrefabAssetKey = parsedValue;
                        else
                            value.Tag = parsedValue;
                        parsed = true;
                    }

                    if (parsed)
                        fieldDefinition.DefaultValue = std::move(value);
                }
                else if (functionSuffix == "Prefab")
                {
                    Prefab value{};
                    if (fallbackExpression == "{}" ||
                        fallbackExpression == "Prefab{}" ||
                        fallbackExpression == "Limitless::Prefab{}" ||
                        fallbackExpression == "ScriptPrefabReference{}" ||
                        fallbackExpression == "Limitless::ScriptPrefabReference{}")
                    {
                        parsed = true;
                    }
                    else if (fallbackExpression.size() >= 2 && fallbackExpression.front() == '"' && fallbackExpression.back() == '"')
                    {
                        value.AssetKey = fallbackExpression.substr(1, fallbackExpression.size() - 2);
                        parsed = true;
                    }

                    if (parsed)
                        fieldDefinition.DefaultValue = std::move(value);
                }

                if (!parsed)
                    continue;

                outFields.push_back(std::move(fieldDefinition));
                existingNames.insert(propertyName);
            }

            outError.clear();
            return true;
        }

        bool SynchronizeExposedPropertiesFromScript(NativeScriptEntry& nativeScript,
                                                    std::vector<std::string>& outOrderedFieldNames,
                                                    std::string& outError)
        {
            outOrderedFieldNames.clear();
            outError.clear();

            if (nativeScript.ScriptClassName.empty())
                return true;

            std::filesystem::path headerPath;
            std::filesystem::path sourcePath;
            if (!ResolveNativeScriptFilePaths(nativeScript.ScriptClassName, nativeScript.ScriptAssetRelativePath, headerPath, sourcePath))
            {
                outError = "Script files not found for class '" + nativeScript.ScriptClassName + "'.";
                return false;
            }

            std::vector<ScriptPublicFieldDefinition> fields;
            if (!ParsePublicScriptFieldsFromHeader(headerPath, fields, outError))
                return false;

            if (fields.empty())
            {
                (void)ParseLegacyExposedFieldsFromSource(sourcePath, fields, outError);
            }

            std::unordered_set<std::string> declaredFieldNames;
            declaredFieldNames.reserve(fields.size());
            for (const auto& field : fields)
            {
                declaredFieldNames.insert(field.Name);
                outOrderedFieldNames.push_back(field.Name);

                const auto found = nativeScript.ExposedProperties.find(field.Name);
                if (found == nativeScript.ExposedProperties.end())
                {
                    nativeScript.ExposedProperties.emplace(field.Name, field.DefaultValue);
                    continue;
                }

                if (found->second.index() != field.DefaultValue.index())
                {
                    if (std::holds_alternative<ScriptEntityReference>(field.DefaultValue))
                    {
                        if (const auto* legacyPrefab = std::get_if<Prefab>(&found->second))
                        {
                            ScriptEntityReference migratedReference{};
                            migratedReference.PrefabAssetKey = legacyPrefab->AssetKey;
                            found->second = std::move(migratedReference);
                            continue;
                        }
                    }
                    else if (std::holds_alternative<Prefab>(field.DefaultValue))
                    {
                        if (const auto* entityReference = std::get_if<ScriptEntityReference>(&found->second))
                        {
                            Prefab migratedReference{};
                            migratedReference.AssetKey = entityReference->PrefabAssetKey;
                            found->second = std::move(migratedReference);
                            continue;
                        }
                    }

                    found->second = field.DefaultValue;
                }
            }

            // Keep undeclared/unsupported properties so authoring data is not dropped
            // during automatic inspector synchronization.

            return true;
        }
}

