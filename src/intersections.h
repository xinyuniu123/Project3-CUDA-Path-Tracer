#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/intersect.hpp>

#include "sceneStructs.h"
#include "utilities.h"

#define BOUNDING_BOX 1
#define BVH_MAX_SIZE 128

/**
 * Handy-dandy hash function that provides seeds for random number generation.
 */
__host__ __device__ inline unsigned int utilhash(unsigned int a) {
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);
    return a;
}

// CHECKITOUT
/**
 * Compute a point at parameter value `t` on ray `r`.
 * Falls slightly short so that it doesn't intersect the object it's hitting.
 */
__host__ __device__ glm::vec3 getPointOnRay(Ray r, float t) {
    return r.origin + (t - .0001f) * glm::normalize(r.direction);
}

/**
 * Multiplies a mat4 and a vec4 and returns a vec3 clipped from the vec4.
 */
__host__ __device__ glm::vec3 multiplyMV(glm::mat4 m, glm::vec4 v) {
    return glm::vec3(m * v);
}

__host__ __device__ bool hasIntersection(const Ray& transformedRay, const BBox& box)
{
    float tmin = -1e38f;
    float tmax = 1e38f;
    glm::vec3 tmin_n;
    glm::vec3 tmax_n;
    for (int xyz = 0; xyz < 3; ++xyz) {
        float qdxyz = transformedRay.direction[xyz];
        float qoxyz = transformedRay.origin[xyz];
        if (glm::abs(qdxyz) > 0.00001f) {
            float t1 = (box.minP[xyz] - qoxyz) / qdxyz;
            float t2 = (box.maxP[xyz] - qoxyz) / qdxyz;
            float ta = glm::min(t1, t2);
            float tb = glm::max(t1, t2);
            glm::vec3 n;
            n[xyz] = t2 < t1 ? +1 : -1;
            if (ta > 0 && ta > tmin) {
                tmin = ta;
                tmin_n = n;
            }
            if (tb < tmax) {
                tmax = tb;
                tmax_n = n;
            }
        }
    }
    return tmax >= tmin && tmax > 0;
}

// CHECKITOUT
/**
 * Test intersection between a ray and a transformed cube. Untransformed,
 * the cube ranges from -0.5 to 0.5 in each axis and is centered at the origin.
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float boxIntersectionTest(Geom box, Ray r,
        glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside) {
    Ray q;
    q.origin    =                multiplyMV(box.inverseTransform, glm::vec4(r.origin   , 1.0f));
    q.direction = glm::normalize(multiplyMV(box.inverseTransform, glm::vec4(r.direction, 0.0f)));

    float tmin = -1e38f;
    float tmax = 1e38f;
    glm::vec3 tmin_n;
    glm::vec3 tmax_n;
    for (int xyz = 0; xyz < 3; ++xyz) {
        float qdxyz = q.direction[xyz];
        /*if (glm::abs(qdxyz) > 0.00001f)*/ {
            float t1 = (-0.5f - q.origin[xyz]) / qdxyz;
            float t2 = (+0.5f - q.origin[xyz]) / qdxyz;
            float ta = glm::min(t1, t2);
            float tb = glm::max(t1, t2);
            glm::vec3 n;
            n[xyz] = t2 < t1 ? +1 : -1;
            if (ta > 0 && ta > tmin) {
                tmin = ta;
                tmin_n = n;
            }
            if (tb < tmax) {
                tmax = tb;
                tmax_n = n;
            }
        }
    }

    if (tmax >= tmin && tmax > 0) {
        outside = true;
        if (tmin <= 0) {
            tmin = tmax;
            tmin_n = tmax_n;
            outside = false;
        }
        intersectionPoint = multiplyMV(box.transform, glm::vec4(getPointOnRay(q, tmin), 1.0f));
        normal = glm::normalize(multiplyMV(box.invTranspose, glm::vec4(tmin_n, 0.0f)));
        return glm::length(r.origin - intersectionPoint);
    }
    return -1;
}

// CHECKITOUT
/**
 * Test intersection between a ray and a transformed sphere. Untransformed,
 * the sphere always has radius 0.5 and is centered at the origin.
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float sphereIntersectionTest(Geom sphere, Ray r,
        glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside) {
    float radius = .5;

    glm::vec3 ro = multiplyMV(sphere.inverseTransform, glm::vec4(r.origin, 1.0f));
    glm::vec3 rd = glm::normalize(multiplyMV(sphere.inverseTransform, glm::vec4(r.direction, 0.0f)));

    Ray rt;
    rt.origin = ro;
    rt.direction = rd;

    float vDotDirection = glm::dot(rt.origin, rt.direction);
    float radicand = vDotDirection * vDotDirection - (glm::dot(rt.origin, rt.origin) - powf(radius, 2));
    if (radicand < 0) {
        return -1;
    }

    float squareRoot = sqrt(radicand);
    float firstTerm = -vDotDirection;
    float t1 = firstTerm + squareRoot;
    float t2 = firstTerm - squareRoot;

    float t = 0;
    if (t1 < 0 && t2 < 0) {
        return -1;
    } else if (t1 > 0 && t2 > 0) {
        t = min(t1, t2);
        outside = true;
    } else {
        t = max(t1, t2);
        outside = false;
    }

    glm::vec3 objspaceIntersection = getPointOnRay(rt, t);

    intersectionPoint = multiplyMV(sphere.transform, glm::vec4(objspaceIntersection, 1.f));
    normal = glm::normalize(multiplyMV(sphere.invTranspose, glm::vec4(objspaceIntersection, 0.f)));
    if (!outside) {
        normal = -normal;
    }

    return glm::length(r.origin - intersectionPoint);
}

__host__ __device__ float triangleIntersectionTest(Geom mesh, Ray r, Triangle* triangles, int triSize,
    glm::vec3& intersectionPoint, glm::vec3& normal, glm::vec2& uv, bool& outside)
{

    Ray q;
    q.origin = multiplyMV(mesh.inverseTransform, glm::vec4(r.origin, 1.0f));
    q.direction = glm::normalize(multiplyMV(mesh.inverseTransform, glm::vec4(r.direction, 0.0f)));

#if BOUNDING_BOX
    if (!hasIntersection(q, mesh.box))
    {
        return -1;
    }
#endif

    float tmin = 1e38f;
    Triangle minTri;
    glm::vec3 minBcenter;

    for (int i = mesh.triStart; i < mesh.triEnd; i++)
    {
        Triangle& tri = triangles[i];
        
        glm::vec3 bcenter;
        bool intersect = glm::intersectRayTriangle(q.origin, q.direction, tri.verts[0], tri.verts[1], tri.verts[2], bcenter);

        if (!intersect) continue;

        float t = bcenter.z;

        if (t < tmin && t > 0.0)
        {
            tmin = t;
            minTri = tri;
            minBcenter = bcenter;
        }
    }

    if (tmin < 1e38f) 
    {
        float b1 = minBcenter[0];
        float b2 = minBcenter[1];
        float b = 1 - b1 - b2;
        normal = b1 * minTri.nors[0] + b2 * minTri.nors[1] + b * minTri.nors[2];
        uv = b1 * minTri.uvs[0] + b2 * minTri.uvs[1] + b * minTri.uvs[2];

        glm::vec3 objspaceIntersection = getPointOnRay(q, tmin);

        intersectionPoint = multiplyMV(mesh.transform, glm::vec4(objspaceIntersection, 1.f));
        normal = glm::normalize(multiplyMV(mesh.invTranspose, glm::vec4(normal, 0.f)));

        outside = glm::dot(normal, q.direction) < 0;

        if (!outside)
        {
            normal = -normal;
        }

        return glm::length(r.origin - intersectionPoint);
    }

    return -1;
}

__host__ __device__ float bvhTriangleIntersectionTest(Geom mesh, Ray r, Triangle* triangles, BVHNode* bvhNodes, int triSize,
    glm::vec3& intersectionPoint, glm::vec3& normal, glm::vec2& uv, bool& outside)
{
    Ray q;
    q.origin = multiplyMV(mesh.inverseTransform, glm::vec4(r.origin, 1.0f));
    q.direction = glm::normalize(multiplyMV(mesh.inverseTransform, glm::vec4(r.direction, 0.0f)));

    BVHNode* curBVH = bvhNodes + mesh.bvhStart;
    Triangle* curTri = triangles + mesh.triStart;

    int bvhStack[BVH_MAX_SIZE];
    bvhStack[0] = 0;
    int endIdx = 0;

    float tmin = FLT_MAX;
    Triangle minTri;
    glm::vec3 minBcenter;

    while (endIdx >= 0 && endIdx < BVH_MAX_SIZE) 
    {
        int curIdx = bvhStack[endIdx];
        endIdx--;
        const BVHNode& curNode = curBVH[curIdx];

        if (!hasIntersection(q, curNode.bbox)) continue;

        if (curNode.nPrimitives > 0) 
        {
            for (int i = curNode.firstPrimOffset; i < curNode.firstPrimOffset + curNode.nPrimitives; i++) 
            {
                const Triangle& tri = curTri[i];
                glm::vec3 bcenter;
                bool intersect = glm::intersectRayTriangle(q.origin, q.direction, tri.verts[0], tri.verts[1], tri.verts[2], bcenter);

                if (!intersect) continue;

                float t = bcenter.z;

                if (t < tmin && t > 0.0)
                {
                    tmin = t;
                    minTri = tri;
                    minBcenter = bcenter;
                }
            }
        }
        else 
        {
            if (curNode.left != -1)
            {
                bvhStack[++endIdx] = curNode.left;
            }
            if (curNode.right != -1)
            {
                bvhStack[++endIdx] = curNode.right;
            }
        }
    }

    if (tmin < 1e38f)
    {
        float b1 = minBcenter[0];
        float b2 = minBcenter[1];
        float b = 1 - b1 - b2;
        normal = b1 * minTri.nors[0] + b2 * minTri.nors[1] + b * minTri.nors[2];
        uv = b1 * minTri.uvs[0] + b2 * minTri.uvs[1] + b * minTri.uvs[2];

        glm::vec3 objspaceIntersection = getPointOnRay(q, tmin);

        intersectionPoint = multiplyMV(mesh.transform, glm::vec4(objspaceIntersection, 1.f));
        normal = glm::normalize(multiplyMV(mesh.invTranspose, glm::vec4(normal, 0.f)));

        outside = glm::dot(normal, q.direction) < 0;

        if (!outside)
        {
            normal = -normal;
        }

        return glm::length(r.origin - intersectionPoint);
    }

    return -1;
}