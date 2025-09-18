#include "memory.hpp"
#include "render_pipeline.hpp"
#include <cstring>


// We try HOST_COHERENT first; if unavailable, we fall back to NON-coherent and will flush for you.
VkResult MappedArena::create(VkDevice device,
                VkPhysicalDevice phys,
                VkDeviceSize capacityBytes,
                VkBufferUsageFlags usage,
                bool preferCoherent)
{
    destroy(device);
    m_device   = device;
    m_capacity = 0;
    m_head     = 0;

    // Query limits (for non-coherent flush alignment)
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys, &props);
    m_atom = std::max<VkDeviceSize>(1, props.limits.nonCoherentAtomSize);

    // Create buffer
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size        = capacityBytes;
    bi.usage       = usage; // e.g. VERTEX_BUFFER_BIT | TRANSFER_SRC_BIT
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device, &bi, nullptr, &m_buffer));

    // Allocate memory
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(device, m_buffer, &mr);

    // Pick memory props
    VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    if (preferCoherent) want |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    uint32_t typeIndex = render::find_mem_type(phys, mr.memoryTypeBits, want);
    if (typeIndex == UINT32_MAX) {
        // Fall back: drop COHERENT requirement if we asked for it
        want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        typeIndex = render::find_mem_type(phys, mr.memoryTypeBits, want);
        if (typeIndex == UINT32_MAX) {
            // No host-visible at all
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        m_isCoherent = false;
    } else {
        // We got what we wanted (may or may not include COHERENT)
        m_isCoherent = ( (want & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0 );
    }

    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize  = mr.size;         // driver-aligned
    ai.memoryTypeIndex = typeIndex;
    VK_CHECK(vkAllocateMemory(device, &ai, nullptr, &m_memory));
    VK_CHECK(vkBindBufferMemory(device, m_buffer, m_memory, 0));

    // Map once, whole size
    VK_CHECK(vkMapMemory(device, m_memory, 0, VK_WHOLE_SIZE, 0, &m_mapped));

    // Capacity is the buffer's logical size (do not write past this)
    m_capacity = bi.size;
    m_usage    = usage;
    m_memProps = want;
    return VK_SUCCESS;
}

// Recreate with a new capacity. All previous offsets/pointers become invalid.
VkResult MappedArena::realloc(VkPhysicalDevice phys, VkDeviceSize newCapacity) {
    // Save parameters to recreate with same usage/flags.
    VkDevice device = m_device;
    VkBufferUsageFlags usage = m_usage;
    bool preferCoherent = (m_memProps & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

    destroy(device);
    return create(device, phys, newCapacity, usage, preferCoherent);
}

// Free GPU resources (safe to call on an uninitialized object).
void MappedArena::destroy(VkDevice device) {
    if (m_mapped) {
        vkUnmapMemory(device, m_memory);
        m_mapped = nullptr;
    }
    if (m_memory) {
        vkFreeMemory(device, m_memory, nullptr);
        m_memory = VK_NULL_HANDLE;
    }
    if (m_buffer) {
        vkDestroyBuffer(device, m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
    }
    m_capacity = 0;
    m_head = 0;
    m_device = VK_NULL_HANDLE;
    m_usage = 0;
    m_memProps = 0;
    m_atom = 1;
}

// Allocate space (optionally aligned) and copy CPU data into it.
// On success, 'out' contains buffer/offset/cpu_ptr for immediate use.
// Returns VK_ERROR_OUT_OF_DEVICE_MEMORY if ring has no room.
VkResult MappedArena::allocAndWrite(const void* src,
                       VkDeviceSize size,
                       UploadAlloc& out,
                       VkDeviceSize align)
{
    if (size == 0) size = 1; // forbid zero-sized nonsense

    VkDeviceSize off = align_up(m_head, align);
    if (off + size > m_capacity) {
        // No wrap-around support: keep it super simple as requested.
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    void* dst = static_cast<char*>(m_mapped) + off;
    std::memcpy(dst, src, size);

    if (!m_isCoherent) {
        // Do an aligned flush
        VkDeviceSize flushOff  = align_down(off, m_atom);
        VkDeviceSize flushEnd  = align_up(off + size, m_atom);
        VkMappedMemoryRange rng{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        rng.memory = m_memory;
        rng.offset = flushOff;
        rng.size   = flushEnd - flushOff;
        vkFlushMappedMemoryRanges(m_device, 1, &rng);
    }

    out.buffer  = m_buffer;
    out.offset  = off;
    out.cpu_ptr = dst;
    out.size    = size;

    m_head = off + size;
    return VK_SUCCESS;
}
