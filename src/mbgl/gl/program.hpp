#pragma once

#include <mbgl/gfx/program.hpp>
#include <mbgl/gl/types.hpp>
#include <mbgl/gl/object.hpp>
#include <mbgl/gl/context.hpp>
#include <mbgl/gl/draw_scope_resource.hpp>
#include <mbgl/gfx/vertex_buffer.hpp>
#include <mbgl/gfx/index_buffer.hpp>
#include <mbgl/gfx/uniform.hpp>
#include <mbgl/gl/vertex_array.hpp>
#include <mbgl/gl/attribute.hpp>
#include <mbgl/gl/uniform.hpp>
#include <mbgl/gl/texture.hpp>
#include <mbgl/util/io.hpp>

#include <mbgl/util/logging.hpp>
#include <mbgl/programs/program_parameters.hpp>
#include <mbgl/shaders/shader_manifest.hpp>

#include <string>
#include <utility> // std::index_sequence (per-attribute warmup)

namespace mbgl {
namespace gl {

namespace detail {
// Every attribute engaged; input for eager shader warmup. See Program::warmup.
template <class>
struct AllBound;
template <class... As>
struct AllBound<TypeList<As...>> {
    static gfx::AttributeBindings<TypeList<As...>> value() {
        return {ExpandToType<As, gfx::AttributeBinding>{}...};
    }
};

// Exactly attribute I missing, all others engaged — mixed-mask warmup variant.
// Mid-drag, a single data-driven attribute flips buffer-backed -> uniform-backed while
// the rest stay buffer-backed; per-single-missing is the minimal covering set for those
// masks (the high-frequency shape in real styles). Double-missing combos (2^N) are NOT
// enumerated — residual risk, see warmup().
template <size_t, class>
struct OneMissing;
template <size_t I, class... As>
struct OneMissing<I, TypeList<As...>> {
    static gfx::AttributeBindings<TypeList<As...>> value() {
        return {(TypeIndex<As, As...>::value == I
                     ? std::optional<gfx::AttributeBinding>()
                     : std::optional<gfx::AttributeBinding>(gfx::AttributeBinding{}))...};
    }
};
} // namespace detail

template <class Name>
class Program final : public gfx::Program<Name> {
public:
    using AttributeList = typename Name::AttributeList;
    using UniformList = typename Name::UniformList;
    using TextureList = typename Name::TextureList;

    Program(ProgramParameters programParameters_)
        : programParameters(std::move(programParameters_)) {}

    const ProgramParameters programParameters;

    class Instance {
    public:
        Instance(Context& context,
                 const std::initializer_list<const char*>& vertexSource,
                 const std::initializer_list<const char*>& fragmentSource)
            : program(context.createProgram(context.createShader(ShaderType::Vertex, vertexSource),
                                            context.createShader(ShaderType::Fragment, fragmentSource),
                                            attributeLocations.getFirstAttribName())) {
            attributeLocations.queryLocations(program);
            uniformStates.queryLocations(program);
            // Texture units are specified via uniforms as well, so we need query their locations
            textureStates.queryLocations(program);
        }

        static std::unique_ptr<Instance> createInstance(gl::Context& context,
                                                        const ProgramParameters& programParameters,
                                                        const std::string& additionalDefines) {
            // Compile the shader
            std::initializer_list<const char*> vertexSource = {
                "#version 300 es\n",
                programParameters.getDefines().c_str(),
                additionalDefines.c_str(),
                shaders::ShaderSource<shaders::BuiltIn::Prelude, gfx::Backend::Type::OpenGL>::vertex,
                programParameters.vertexSource(gfx::Backend::Type::OpenGL).c_str()};

            std::initializer_list<const char*> fragmentSource = {
                "#version 300 es\n",
                programParameters.getDefines().c_str(),
                additionalDefines.c_str(),
                shaders::ShaderSource<shaders::BuiltIn::Prelude, gfx::Backend::Type::OpenGL>::fragment,
                programParameters.fragmentSource(gfx::Backend::Type::OpenGL).c_str()};

            return std::make_unique<Instance>(context, vertexSource, fragmentSource);
        }

        UniqueProgram program;
        gl::AttributeLocations<AttributeList> attributeLocations;
        gl::UniformStates<UniformList> uniformStates;
        gl::TextureStates<TextureList> textureStates;
    };

    void draw(gfx::Context& genericContext,
              gfx::RenderPass&,
              const gfx::DrawMode& drawMode,
              const gfx::DepthMode& depthMode,
              const gfx::StencilMode& stencilMode,
              const gfx::ColorMode& colorMode,
              const gfx::CullFaceMode& cullFaceMode,
              const gfx::UniformValues<UniformList>& uniformValues,
              gfx::DrawScope& drawScope,
              const gfx::AttributeBindings<AttributeList>& attributeBindings,
              const gfx::TextureBindings<TextureList>& textureBindings,
              const gfx::IndexBuffer& indexBuffer,
              std::size_t indexOffset,
              std::size_t indexLength) override {
        auto& context = static_cast<gl::Context&>(genericContext);

        context.setDepthMode(depthMode);
        context.setStencilMode(stencilMode);
        context.setColorMode(colorMode);
        context.setCullFaceMode(cullFaceMode);

        Instance* instancePtr = nullptr;
        try {
            instancePtr = &instanceFor(context, attributeBindings);
        } catch (const std::runtime_error& e) {
            Log::Error(Event::OpenGL, e.what());
            return;
        }
        auto& instance = *instancePtr;

        context.program = instance.program;

        instance.uniformStates.bind(uniformValues);

        instance.textureStates.bind(context, textureBindings);

        auto& vertexArray = drawScope.getResource<gl::DrawScopeResource>().vertexArray;
        vertexArray.bind(context, indexBuffer, instance.attributeLocations.toBindingArray(attributeBindings));

        context.draw(drawMode, indexOffset, indexLength);
    }

    // eager warmup: first-drag jank — HXMapWidgetNative black-flash campaign; upstream analog: Metal pipeline cache PR #2379
    // v2: also warms per-attribute-missing masks (exactly attribute i uniform-backed, rest
    // buffer-backed) — the mixed shape hit when one data-driven property flips mid-drag
    // (session 27: v1's 2 canonical variants still stalled 1/3 rounds).
    // Cost: (2 + N) instances x ~27 programs = 150-270 one-time compiles, ~0.5-1 s during
    // style load — user-invisible, runs once, results cached in `instances`.
    // Residual: double-missing masks (2^N) un-enumerated; if stalls persist after v2,
    // escalate to full style-static-analysis variant enumeration (expensive, decide then).
    void warmup(gfx::Context& genericContext) {
        auto& context = static_cast<gl::Context&>(genericContext);

        const gfx::AttributeBindings<AttributeList> noAttributes;
        const gfx::AttributeBindings<AttributeList> allAttributes = detail::AllBound<AttributeList>::value();

        for (const auto* bindings : {&noAttributes, &allAttributes}) {
            warmBindings(context, *bindings);
        }
        warmPerMissing(context, AttributeList{});
    }

private:
    void warmBindings(gl::Context& context, const gfx::AttributeBindings<AttributeList>& bindings) {
        try {
            auto& instance = instanceFor(context, bindings);
            // One-time use forces driver-side pipeline finalization
            context.program = instance.program;
        } catch (const std::exception& e) {
            Log::Error(Event::OpenGL, std::string("shader warmup failed: ") + e.what());
        } catch (...) {
            Log::Error(Event::OpenGL, "shader warmup failed");
        }
    }

    template <class... As>
    void warmPerMissing(gl::Context& context, TypeList<As...>) {
        warmPerMissing(context, std::make_index_sequence<sizeof...(As)>{}, TypeList<As...>{});
    }

    template <size_t... Is, class... As>
    void warmPerMissing(gl::Context& context, std::index_sequence<Is...>, TypeList<As...>) {
        util::ignore({(warmBindings(context, detail::OneMissing<Is, TypeList<As...>>::value()), 0)...});
    }

    Instance& instanceFor(Context& context, const gfx::AttributeBindings<AttributeList>& attributeBindings) {
        const uint32_t key = gl::AttributeKey<AttributeList>::compute(attributeBindings);
        auto it = instances.find(key);
        if (it == instances.end()) {
            it = instances
                     .emplace(key,
                              Instance::createInstance(context,
                                                       programParameters,
                                                       gl::AttributeKey<AttributeList>::defines(attributeBindings)))
                     .first;
        }
        return *it->second;
    }

    std::map<uint32_t, std::unique_ptr<Instance>> instances;
};

} // namespace gl
} // namespace mbgl
