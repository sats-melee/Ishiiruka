// Regression for publishing a vertex loader before its native format is ready.
// Compile the real cache implementation with a minimal graphics backend.
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <thread>
#include "../../Source/Core/VideoCommon/VertexLoaderManager.cpp"

Statistics stats{};
std::unique_ptr<VertexManagerBase> g_vertex_manager;
VertexManagerBase::VertexManagerBase() = default;
VertexManagerBase::~VertexManagerBase() = default;
VertexLoaderBase::VertexLoaderBase(const TVtxDesc&, const VAT&)
    : m_VertexSize(4), m_native_stride(12), m_native_vtx_decl{},
      m_native_components(0), m_native_vertex_format(nullptr), m_numLoadedVertices(0) {}
VertexLoaderUID::VertexLoaderUID(const TVtxDesc&, const VAT&) : vid{}, hash(0), platformhash(0) {}
bool VertexLoaderUID::operator==(const VertexLoaderUID& other) const { return hash == other.hash; }
size_t VertexLoaderUID::GetplatformHash() const { return platformhash; }

class TestLoader final : public VertexLoaderBase
{
public:
    TestLoader(const TVtxDesc& desc, const VAT& vat) : VertexLoaderBase(desc, vat) {}
    bool IsInitialized() override { return true; }
    s32 RunVertices(const VertexLoaderParameters&) override { return 0; }
};
std::unique_ptr<VertexLoaderBase> VertexLoaderBase::CreateVertexLoader(const TVtxDesc& desc, const VAT& vat)
{
    return std::make_unique<TestLoader>(desc, vat);
}
class TestFormat final : public NativeVertexFormat
{
public:
    explicit TestFormat(const PortableVertexDeclaration& decl) { vtx_decl = decl; }
    void SetupVertexPointers() override {}
};
std::promise<void> format_entered;
std::promise<void> finish_format;
auto format_release = finish_format.get_future().share();
std::atomic<int> format_creations{0};
class TestManager final : public VertexManagerBase
{
public:
    std::unique_ptr<NativeVertexFormat> CreateNativeVertexFormat(const PortableVertexDeclaration& decl) override
    {
        ++format_creations;
        format_entered.set_value();
        format_release.wait();
        return std::make_unique<TestFormat>(decl);
    }
    void PrepareShaders(PrimitiveType, u32, const XFMemory&, const BPMemory&, bool) override {}
    void ResetBuffer(u32) override {}
    void vFlush(bool) override {}
    u16* GetIndexBuffer() override { return nullptr; }
};
int main()
{
    g_vertex_manager = std::make_unique<TestManager>();
    TVtxDesc desc{};
    VAT vat{};
    auto producer = std::async(std::launch::async, [&] { return VertexLoaderManager::GetOrAddLoader(desc, vat); });
    format_entered.get_future().wait();
    std::promise<void> consumer_started;
    auto consumer = std::async(std::launch::async, [&] {
        consumer_started.set_value();
        return VertexLoaderManager::GetOrAddLoader(desc, vat)->m_native_vertex_format != nullptr;
    });
    consumer_started.get_future().wait();
    const bool completed_early = consumer.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready;
    finish_format.set_value();
    auto* loader = producer.get();
    const bool ready = consumer.get();
    if (completed_early || !ready || !loader->m_native_vertex_format || format_creations != 1)
    {
        std::cerr << "FAIL: a concurrent consumer observed a partially initialized vertex loader\n";
        return 1;
    }
    std::cout << "PASS: concurrent vertex cache consumers receive one complete native format\n";
}
