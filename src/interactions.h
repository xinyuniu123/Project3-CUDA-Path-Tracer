#pragma once

#include "intersections.h"
#include <thrust/random.h>

// CHECKITOUT
/**
 * Computes a cosine-weighted random direction in a hemisphere.
 * Used for diffuse lighting.
 */
__host__ __device__
glm::vec3 calculateRandomDirectionInHemisphere(
        glm::vec3 normal, thrust::default_random_engine &rng) {
    thrust::uniform_real_distribution<float> u01(0, 1);

    float up = sqrt(u01(rng)); // cos(theta)
    float over = sqrt(1 - up * up); // sin(theta)
    float around = u01(rng) * TWO_PI;

    // Find a direction that is not the normal based off of whether or not the
    // normal's components are all equal to sqrt(1/3) or whether or not at
    // least one component is less than sqrt(1/3). Learned this trick from
    // Peter Kutz.

    glm::vec3 directionNotNormal;
    if (abs(normal.x) < SQRT_OF_ONE_THIRD) {
        directionNotNormal = glm::vec3(1, 0, 0);
    } else if (abs(normal.y) < SQRT_OF_ONE_THIRD) {
        directionNotNormal = glm::vec3(0, 1, 0);
    } else {
        directionNotNormal = glm::vec3(0, 0, 1);
    }

    // Use not-normal direction to generate two perpendicular directions
    glm::vec3 perpendicularDirection1 =
        glm::normalize(glm::cross(normal, directionNotNormal));
    glm::vec3 perpendicularDirection2 =
        glm::normalize(glm::cross(normal, perpendicularDirection1));

    return up * normal
        + cos(around) * over * perpendicularDirection1
        + sin(around) * over * perpendicularDirection2;
}

/**
 * Scatter a ray with some probabilities according to the material properties.
 * For example, a diffuse surface scatters in a cosine-weighted hemisphere.
 * A perfect specular surface scatters in the reflected ray direction.
 * In order to apply multiple effects to one surface, probabilistically choose
 * between them.
 *
 * The visual effect you want is to straight-up add the diffuse and specular
 * components. You can do this in a few ways. This logic also applies to
 * combining other types of materias (such as refractive).
 *
 * - Always take an even (50/50) split between a each effect (a diffuse bounce
 *   and a specular bounce), but divide the resulting color of either branch
 *   by its probability (0.5), to counteract the chance (0.5) of the branch
 *   being taken.
 *   - This way is inefficient, but serves as a good starting point - it
 *     converges slowly, especially for pure-diffuse or pure-specular.
 * - Pick the split based on the intensity of each material color, and divide
 *   branch result by that branch's probability (whatever probability you use).
 *
 * This method applies its changes to the Ray parameter `ray` in place.
 * It also modifies the color `color` of the ray in place.
 *
 * You may need to change the parameter list for your purposes!
 */
 __device__
void scatterRay(
        PathSegment & pathSegment,
        glm::vec3 intersect,
        glm::vec3 normal,
        glm::vec2 uv,
        const Material &m,
        thrust::default_random_engine &rng,
        cudaTextureObject_t* texObjects,
        Texture* textures,
        int texId) {
    // TODO: implement this.
    // A basic implementation of pure-diffuse shading will just call the
    // calculateRandomDirectionInHemisphere defined above.
    thrust::uniform_real_distribution<float> u01(0, 1);

    glm::vec3 texColor;

    if (texId != -1) 
    {

        cudaTextureObject_t texObject = texObjects[texId];

        float u = uv[0];
        float v = uv[1];

        float4 finalcol = tex2D<float4>(texObject, u, v);
        texColor = glm::vec3(finalcol.x, finalcol.y, finalcol.z);

    }
    else 
    {
        texColor = m.color;
    }

    // Pure Diffuse
    if (!m.hasReflective && !m.hasRefractive) 
    {
        glm::vec3 direction = glm::normalize(calculateRandomDirectionInHemisphere(normal, rng));
        pathSegment.ray.origin = intersect + EPSILON * normal;
        pathSegment.ray.direction = direction;
        pathSegment.color *= texColor;
    }
    // Reflective
    else if (m.hasReflective && !m.hasRefractive) 
    {
        glm::vec3 direction = glm::reflect(pathSegment.ray.direction, normal);
        pathSegment.ray.origin = intersect + EPSILON * normal;
        pathSegment.ray.direction = glm::normalize(direction);
        pathSegment.color *= texColor;
    }
    // Refraction and Reflection
    else if(m.hasReflective && m.hasRefractive)
    {
        glm::vec3 rayDir = pathSegment.ray.direction;
        float cosTheta = glm::dot(normal, -rayDir);
        float n1 = 1.0f;
        float n2 = 1.0f;

        if (cosTheta >= 0.f) 
        {
            n2 = m.indexOfRefraction;
        }
        else 
        {
            normal = glm::normalize(-normal);
            n1 = m.indexOfRefraction;
        }

        float R0 = pow((n1 - n2) / (n1 + n2), 2);
        float R = R0 + (1.f - R0) * pow((1 - cosTheta), 5);

        thrust::uniform_real_distribution<float> u01(0, 1);
        if (u01(rng) >= R) // Refraction
        {
            glm::vec3 direction = glm::normalize(glm::refract(rayDir, normal, n1 / n2));
            pathSegment.ray.direction = direction;
            pathSegment.ray.origin = intersect + EPSILON * pathSegment.ray.direction;
            pathSegment.color *= texColor;
        }
        else // Reflection 
        {
            glm::vec3 direction = glm::reflect(rayDir, normal);
            pathSegment.ray.origin = intersect + EPSILON * normal;
            pathSegment.ray.direction = glm::normalize(direction);
            pathSegment.color *= texColor;
        }
    }
}
