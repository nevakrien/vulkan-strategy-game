#ifndef MEMORY_HPP
#define MEMORY_HPP

#include <vulkan/vulkan.h>
#include <common.hpp>

inline VkDeviceSize align_up(VkDeviceSize v, VkDeviceSize a) {
    return (a ? (v + (a - 1)) / a * a : v);
}
inline VkDeviceSize align_down(VkDeviceSize v, VkDeviceSize a) {
    return (a ? v / a * a : v);
}

struct UploadAlloc {
    VkBuffer       buffer = VK_NULL_HANDLE;
    VkDeviceSize   offset = 0;     // offset you can bind/use
    void*          cpu_ptr = nullptr; // write here
    VkDeviceSize   size   = 0;     // requested size
};

// --- The actual ring ---
class MappedArena {
public:
    // Create a persistently mapped ring buffer
    VkResult create(VkDevice device,
                    VkPhysicalDevice phys,
                    VkDeviceSize capacityBytes,
                    VkBufferUsageFlags usage,
                    bool preferCoherent = true);

    // Recreate with a new capacity (invalidates previous allocations)
    VkResult realloc(VkPhysicalDevice phys, VkDeviceSize newCapacity);
    inline VkResult maybe_realloc(VkPhysicalDevice phys, VkDeviceSize newCapacity){
    	if (newCapacity<=capacity())
    		return VK_SUCCESS;

    	return realloc(phys,newCapacity);
    }

    // Free GPU resources
    void destroy(VkDevice device);

    // Reset ring for a new frame (caller ensures GPU finished with it)
    inline void reset() {
        m_head = 0;
    }


    inline void assert_matches(VkBufferUsageFlags need) const{
    	DEBUG_ASSERT((usage() & need)==need);
    }

    // Allocate space and copy CPU data into it
    VkResult allocAndWrite(const void* src,
                           VkDeviceSize size,
                           UploadAlloc& out,
                           VkDeviceSize align = 16);

    // --- Accessors ---
    VkBuffer                 buffer()     const { return m_buffer; }
    VkDeviceMemory           memory()     const { return m_memory; }
    VkDeviceSize             capacity()   const { return m_capacity; }
    VkDeviceSize             used()       const { return m_head; }
    bool                     isCoherent() const { return m_isCoherent; }
    VkDeviceSize             atomSize()   const { return m_atom; }
    VkBufferUsageFlags       usage()      const { return m_usage; }
    VkMemoryPropertyFlags    memProps()   const { return m_memProps; }

private:
    VkDevice        m_device  = VK_NULL_HANDLE;
    VkBuffer        m_buffer  = VK_NULL_HANDLE;
    VkDeviceMemory  m_memory  = VK_NULL_HANDLE;
    void*           m_mapped  = nullptr;

    VkDeviceSize    m_capacity = 0;  // buffer size in bytes
    VkDeviceSize    m_head     = 0;  // next free offset

    VkDeviceSize           m_atom      = 1;  // nonCoherentAtomSize
    bool                   m_isCoherent = true;
    VkBufferUsageFlags     m_usage     = 0;
    VkMemoryPropertyFlags  m_memProps  = 0;
};

#endif // MEMORY_HPP

