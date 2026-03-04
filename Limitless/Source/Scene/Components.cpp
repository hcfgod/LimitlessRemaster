#include "Scene/Components/CoreComponents.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Limitless
{
    glm::mat4 TransformComponent::GetLocalMatrix() const
    {
        glm::mat4 matrix = glm::mat4(1.0f);
        matrix = glm::translate(matrix, Position);
        matrix = glm::rotate(matrix, glm::radians(Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        matrix = glm::rotate(matrix, glm::radians(Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        matrix = glm::rotate(matrix, glm::radians(Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        matrix = glm::scale(matrix, Scale);
        return matrix;
    }
}
