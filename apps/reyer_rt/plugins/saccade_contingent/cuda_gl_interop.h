#ifndef CUDA_GL_INTEROP_H
#define CUDA_GL_INTEROP_H

#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include <cuda.h>
#include <GL/gl.h>
#include <iostream>
#include <stdexcept>
#include <string>

/**
 * CUDA-OpenGL interoperability wrapper for efficient texture operations
 * Handles registration, mapping, and unmapping of OpenGL textures with CUDA
 */
class CudaGLInterop
{
private:
    cudaGraphicsResource_t resource = nullptr;
    cudaSurfaceObject_t surface_obj = 0;
    bool is_mapped = false;

    /**
     * Check CUDA errors and throw exceptions with descriptive messages
     */
    void checkCudaError(cudaError_t err, const char *context)
    {
        if (err != cudaSuccess)
        {
            std::string error_msg = std::string(context) + ": " + std::string(cudaGetErrorString(err));
            throw std::runtime_error(error_msg);
        }
    }

    /**
     * Check cuGraphics errors and throw exceptions
     */
    void checkGraphicsError(cudaError_t err, const char *context)
    {
        if (err != cudaSuccess)
        {
            std::string error_msg = std::string(context) + ": " + std::string(cudaGetErrorString(err));
            throw std::runtime_error(error_msg);
        }
    }

public:
    CudaGLInterop() = default;

    /**
     * Register an OpenGL texture with CUDA for interoperability
     * @param texture_id The OpenGL texture ID (GLuint)
     * @throws std::runtime_error if registration fails
     */
    void registerTexture(GLuint texture_id)
    {
        // Clean up previous registration if exists
        if (resource != nullptr)
        {
            unregisterTexture();
        }

        cudaError_t err = cudaGraphicsGLRegisterImage(
            &resource,
            texture_id,
            GL_TEXTURE_2D,
            cudaGraphicsRegisterFlagsWriteDiscard);

        checkGraphicsError(err, "Failed to register OpenGL texture with CUDA");
    }

    /**
     * Map the registered texture for CUDA access
     * @return cudaSurfaceObject_t that can be used in CUDA kernels to write to the texture
     * @throws std::runtime_error if mapping fails
     */
    cudaSurfaceObject_t mapTexture()
    {
        if (resource == nullptr)
        {
            throw std::runtime_error("Texture not registered. Call registerTexture() first.");
        }

        if (is_mapped)
        {
            throw std::runtime_error("Texture already mapped. Call unmapTexture() before mapping again.");
        }

        // Map the resource
        cudaError_t err = cudaGraphicsMapResources(1, &resource, 0);
        checkCudaError(err, "Failed to map CUDA graphics resource");
        is_mapped = true;

        // Get the array from the mapped resource
        cudaArray_t cuda_array;
        err = cudaGraphicsSubResourceGetMappedArray(&cuda_array, resource, 0, 0);
        checkCudaError(err, "Failed to get mapped array from resource");

        // Create surface object from the array
        cudaResourceDesc resource_desc;
        memset(&resource_desc, 0, sizeof(resource_desc));
        resource_desc.resType = cudaResourceTypeArray;
        resource_desc.res.array.array = cuda_array;

        err = cudaCreateSurfaceObject(&surface_obj, &resource_desc);
        checkCudaError(err, "Failed to create surface object");

        return surface_obj;
    }

    /**
     * Unmap the texture to release CUDA access
     * Must be called after all CUDA operations on the texture are complete
     */
    void unmapTexture()
    {
        if (!is_mapped)
        {
            return;
        }

        // Destroy the surface object
        if (surface_obj != 0)
        {
            cudaError_t err = cudaDestroySurfaceObject(surface_obj);
            if (err != cudaSuccess)
            {
                std::cerr << "Warning: Failed to destroy surface object: " << cudaGetErrorString(err) << std::endl;
            }
            surface_obj = 0;
        }

        // Unmap the resource
        if (resource != nullptr)
        {
            cudaError_t err = cudaGraphicsUnmapResources(1, &resource, 0);
            if (err != cudaSuccess)
            {
                std::cerr << "Warning: Failed to unmap CUDA graphics resource: " << cudaGetErrorString(err) << std::endl;
            }
        }

        is_mapped = false;
    }

    /**
     * Unregister the texture with CUDA
     */
    void unregisterTexture()
    {
        // First unmaps if mapped
        unmapTexture();

        if (resource != nullptr)
        {
            cudaError_t err = cudaGraphicsUnregisterResource(resource);
            if (err != cudaSuccess)
            {
                std::cerr << "Warning: Failed to unregister CUDA graphics resource: " << cudaGetErrorString(err) << std::endl;
            }
            resource = nullptr;
        }
    }

    /**
     * Check if texture is currently mapped
     */
    bool isMapped() const
    {
        return is_mapped;
    }

    /**
     * Destructor - automatically unmaps and unregisters texture
     */
    ~CudaGLInterop()
    {
        unregisterTexture();
    }
};

#endif // CUDA_GL_INTEROP_H
