#include "../std_include.hpp"

#include "node.hpp"
#include "bulk.hpp"
#include "rocktree.hpp"

#include "rocktree_proto.hpp"

#pragma warning(push)
#pragma warning(disable : 4100)
#pragma warning(disable : 4127)
#pragma warning(disable : 4244)
#pragma warning(disable : 6262)

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <crn.h>

#pragma warning(pop)

namespace
{
    int unpack_var_int(const std::string& packed, int* index)
    {
        const auto* data = reinterpret_cast<const uint8_t*>(packed.data());
        const auto size = packed.size();

        int c = 0, d = 1, e{};
        do
        {
            if (*index >= static_cast<int>(size))
            {
                break;
            }

            e = data[(*index)++];
            c += (e & 0x7F) * d;
            d <<= 7;
        } while (e & 0x80);

        return c;
    }

    std::vector<vertex> unpack_vertices(const std::string& packed)
    {
        const auto count = packed.size() / 3;
        const auto data = reinterpret_cast<const uint8_t*>(packed.data());

        uint8_t x = 0, y = 0, z = 0;

        auto vertices = std::vector<vertex>(count);

        for (size_t i = 0; i < count; i++)
        {
            x += data[count * 0 + i];
            y += data[count * 1 + i];
            z += data[count * 2 + i];

            vertices[i].position.x = x;
            vertices[i].position.y = y;
            vertices[i].position.z = z;
        }

        return vertices;
    }

    uint8_t double_to_uint8(const double v)
    {
        const auto cr = static_cast<int32_t>(round(v));

        if (cr < 0)
        {
            return 0;
        }

        if (cr > 255)
        {
            return 255;
        }

        return static_cast<uint8_t>(cr);
    }

    int32_t transform_value(const int32_t v, const int32_t l)
    {
        if (4 >= l)
        {
            return (v << l) + (v & (1 << l) - 1);
        }

        if (6 >= l)
        {
            const auto r = 8 - l;
            return (v << l) + (v << l >> r) + (v << l >> r >> r) + (v << l >> r >> r >> r);
        }

        return -(v & 1);
    }

    std::vector<uint8_t> unpack_for_normals(const NodeData& nodeData)
    {
        if (!nodeData.has_for_normals())
        {
            return {};
        }

        const auto& input = nodeData.for_normals();
        if (input.size() <= 2)
        {
            return {};
        }

        const auto* data = reinterpret_cast<const uint8_t*>(input.data());
        const size_t count = *reinterpret_cast<const uint16_t*>(data);

        if (count * 2 != input.size() - 3)
        {
            return {};
        }

        const int32_t s = data[2];
        data += 3;

        std::vector<uint8_t> output(count * 3);

        for (size_t i = 0; i < count; i++)
        {
            double a = transform_value(data[0 + i], s) / 255.0;
            const double f = transform_value(data[count + i], s) / 255.0;

            double b = a, c = f, g = b + c, h = b - c;
            int sign = 1;

            if (.5 > g || 1.5 < g || -.5 > h || .5 < h)
            {
                sign = -1;
                if (.5 >= g)
                {
                    b = .5 - f;
                    c = .5 - a;
                }
                else
                {
                    if (1.5 <= g)
                    {
                        b = 1.5 - f;
                        c = 1.5 - a;
                    }
                    else
                    {
                        if (-.5 >= h)
                        {
                            b = f - .5;
                            c = a + .5;
                        }
                        else
                        {
                            b = f + .5;
                            c = a - .5;
                        }
                    }
                }

                g = b + c;
                h = b - c;
            }

            a = fmin(fmin(2 * g - 1, 3 - 2 * g), fmin(2 * h + 1, 1 - 2 * h)) * sign;

            b = 2 * b - 1;
            c = 2 * c - 1;

            const auto m = 127 / sqrt(a * a + b * b + c * c);

            output[3 * i + 0] = double_to_uint8(m * a + 127);
            output[3 * i + 1] = double_to_uint8(m * b + 127);
            output[3 * i + 2] = double_to_uint8(m * c + 127);
        }

        return output;
    }

    void unpackNormals(const Mesh& mesh, std::vector<vertex>& vertices, const std::vector<uint8_t>& for_normals)
    {
        if (!mesh.has_normals() || for_normals.empty())
        {
            return;
        }

        const auto count = vertices.size();
        const auto& normals = mesh.normals();
        const auto* input = reinterpret_cast<const uint8_t*>(normals.data());

        if (count * 2 != normals.size())
        {
            return;
        }

        for (size_t i = 0; i < count; ++i)
        {
            const size_t j = input[i] + (input[count + i] << 8);

            if (3 * j + 2 < for_normals.size())
            {
                vertices[i].normal.x = for_normals[3 * j + 0];
                vertices[i].normal.y = for_normals[3 * j + 1];
                vertices[i].normal.z = for_normals[3 * j + 2];
            }
        }
    }

    void unpack_tex_coords(const std::string& packed, std::vector<vertex>& vertices, glm::vec2& uv_offset, glm::vec2& uv_scale)
    {
        const auto count = vertices.size();
        auto data = reinterpret_cast<const uint8_t*>(packed.data());

        if (packed.size() < 4 || count * 4 != packed.size() - 4)
        {
            return;
        }

        const auto u_mod = 1 + *reinterpret_cast<const uint16_t*>(data + 0);
        const auto v_mod = 1 + *reinterpret_cast<const uint16_t*>(data + 2);
        data += 4;

        auto u = 0, v = 0;
        for (size_t i = 0; i < count; i++)
        {
            u = (u + data[count * 0 + i] + (data[count * 2 + i] << 8)) % u_mod;
            v = (v + data[count * 1 + i] + (data[count * 3 + i] << 8)) % v_mod;

            vertices[i].u = static_cast<uint16_t>(u);
            vertices[i].v = static_cast<uint16_t>(v);
        }

        uv_offset[0] = 0.5;
        uv_offset[1] = 0.5;

        uv_scale[0] = static_cast<float>(1.0 / u_mod);
        uv_scale[1] = static_cast<float>(1.0 / v_mod);
    }

    std::vector<uint16_t> unpack_indices(const std::string& packed)
    {
        auto offset = 0;

        const auto triangle_strip_len = unpack_var_int(packed, &offset);
        auto triangle_strip = std::vector<uint16_t>(triangle_strip_len);
        for (int zeros = 0, c = 0, i = 0; i < triangle_strip_len; ++i)
        {
            const int val = unpack_var_int(packed, &offset);

            c = zeros - val;

            triangle_strip[i] = static_cast<uint16_t>(c);
            if (0 == val)
                zeros++;
        }

        return triangle_strip;
    }

    void unpack_octant_mask_and_octant_counts_and_layer_bounds(const std::string& packed, const std::vector<uint16_t>& indices,
                                                               std::vector<vertex>& vertices, int layer_bounds[10])
    {
        // todo: octant counts
        auto offset = 0;
        const auto len = unpack_var_int(packed, &offset);
        auto idx_i = 0;
        auto k = 0;
        auto m = 0;

        for (auto i = 0; i < len; i++)
        {
            if (0 == i % 8)
            {
                if (m < 10)
                {
                    layer_bounds[m++] = k;
                }
            }

            const auto v = unpack_var_int(packed, &offset);
            for (auto j = 0; j < v; j++)
            {
                const auto idx = indices[idx_i++];
                if (idx < indices.size())
                {
                    const auto vtx_i = idx;
                    if (vtx_i < vertices.size())
                    {
                        vertices[vtx_i].octant_mask = i & 7;
                    }
                }
            }
            k += v;
        }

        for (; 10 > m; m++)
        {
            layer_bounds[m] = k;
        }
    }
}

node::node(rocktree& rocktree, const bulk& parent, static_node_data&& sdata)
    : rocktree_object(rocktree, &parent),
      sdata_(std::move(sdata))
{
}

std::string node::get_filename() const
{
    const auto texture_format = std::to_string(this->sdata_.format == texture_format::rgb ? Texture_Format_JPG : Texture_Format_DXT1);

    const auto path = this->sdata_.path.to_string();

    if (this->sdata_.imagery_epoch)
    {
        return "pb=!1m2!1s" + path                                   //
               + "!2u" + std::to_string(this->sdata_.epoch)          //
               + "!2e" + texture_format                              //
               + "!3u" + std::to_string(*this->sdata_.imagery_epoch) //
               + "!4b0";
    }

    return "pb=!1m2!1s" + path                          //
           + "!2u" + std::to_string(this->sdata_.epoch) //
           + "!2e" + texture_format                     //
           + "!4b0";
}

std::string node::get_url() const
{
    return "NodeData/" + this->get_filename();
}

std::filesystem::path node::get_filepath() const
{
    return "NodeData" / octant_path_to_directory(this->sdata_.path.to_string()) / this->get_filename();
}

void node::populate(const std::optional<std::string>& data)
{
    NodeData node_data{};
    if (!data || !node_data.ParseFromString(*data))
    {
        throw std::runtime_error{"Failed to fetch node"};
    }

    if (node_data.matrix_globe_from_mesh_size() == 16)
    {
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                this->matrix_globe_from_mesh[i][j] = node_data.matrix_globe_from_mesh(4 * i + j);
            }
        }
    }

    this->vertices_ = 0;
    this->meshes_.reserve(static_cast<size_t>(node_data.meshes_size()));

    for (const auto& mesh : node_data.meshes())
    {
        mesh_data m{};

        m.indices = unpack_indices(mesh.indices());
        m.vertices = unpack_vertices(mesh.vertices());

        const auto forNormals = unpack_for_normals(node_data);
        unpackNormals(mesh, m.vertices, forNormals);
        unpack_tex_coords(mesh.texture_coordinates(), m.vertices, m.uv_offset, m.uv_scale);
        if (mesh.uv_offset_and_scale_size() == 4)
        {
            m.uv_offset[0] = mesh.uv_offset_and_scale(0);
            m.uv_offset[1] = mesh.uv_offset_and_scale(1);
            m.uv_scale[0] = mesh.uv_offset_and_scale(2);
            m.uv_scale[1] = mesh.uv_offset_and_scale(3);
        }

        int layer_bounds[10];
        unpack_octant_mask_and_octant_counts_and_layer_bounds(mesh.layer_and_octant_counts(), m.indices, m.vertices, layer_bounds);
        if (layer_bounds[3] < 0 || layer_bounds[3] > m.indices.size())
        {
            continue;
        }

        // m.indices_len = layer_bounds[3]; // enable
        m.indices.resize(layer_bounds[3]);

        auto textures = mesh.texture();
        if (textures.size() != 1 || textures[0].data().size() != 1)
        {
            continue;
        }

        auto texture = textures[0];
        auto tex = texture.data()[0];

        // maybe: keep compressed in memory?
        if (texture.format() == Texture_Format_JPG)
        {
            auto tex_data = reinterpret_cast<uint8_t*>(tex.data());
            int width{}, height{}, comp{};
            unsigned char* pixels = stbi_load_from_memory(&tex_data[0], static_cast<int>(tex.size()), &width, &height, &comp, 0);
            if (!pixels)
            {
                continue;
            }

            assert(width == texture.width() && height == texture.height() && comp == 3);
            m.texture = std::vector<uint8_t>(pixels, pixels + width * height * comp);
            stbi_image_free(pixels);
            m.format = texture_format::rgb;
        }
        else if (texture.format() == Texture_Format_CRN_DXT1)
        {
            auto src_size = tex.size();
            auto src = reinterpret_cast<uint8_t*>(tex.data());
            auto dst_size = crn_get_decompressed_size(src, static_cast<uint32_t>(src_size), 0);
            assert(dst_size == ((texture.width() + 3) / 4) * ((texture.height() + 3) / 4) * 8);
            m.texture = std::vector<uint8_t>(dst_size);
            crn_decompress(src, static_cast<uint32_t>(src_size), m.texture.data(), dst_size, 0);
            m.format = texture_format::dxt1;
        }
        else
        {
            throw std::runtime_error("Unsupported texture format: " + std::to_string(texture.format()));
        }

        m.texture_width = static_cast<int>(texture.width());
        m.texture_height = static_cast<int>(texture.height());

        this->vertices_ += m.vertices.size();
        this->meshes_.emplace_back(std::move(m));
    }

    this->meshes_.shrink_to_fit();
}

void node::clear()
{
    this->meshes_ = {};
    this->vertices_ = 0;
}
