#include <iostream>
#include <fmt/format.h>

#include <fstream>
#include <iterator>
#include <utility>
#include <vector>
#include <array>
#include <optional>

// structure sources : https://community.bistudio.com/wiki/P3D_File_Format_-_MLOD

using mlod_signature = std::array<char, 4>;

class mlod_error
{
public:
	explicit mlod_error(std::string err) : error(std::move(err)) {}

	std::string error{};
};

class binary_writer
{
public:
	binary_writer() = default;

	template<typename T>
	void write(const T& d)
	{
		data.resize(data.size() + sizeof(T));
		*reinterpret_cast<T*>(data.data() + write_offset) = d;
		write_offset += sizeof(T);
	}
	
	std::vector<std::uint8_t> data{};
	std::uint64_t write_offset{};
};

// largely untested for issues
class binary_reader
{
public:
	binary_reader(const std::uint8_t* data, const std::uint64_t size)
	{
		current_offset = 0;
		begin	= reinterpret_cast<std::uint64_t>(data);
		end		= reinterpret_cast<std::uint64_t>(data) + size;
	}

	template<typename T>
	bool read(T& out)
	{
		const auto max_read_ptr = begin + current_offset + sizeof(T);

		if (max_read_ptr > end)
			return false;
		
		out = *reinterpret_cast<T*>(begin + current_offset);
		current_offset += sizeof(T);
		return true;
	}
	
	std::uint64_t current_offset;
	std::uint64_t end;
	std::uint64_t begin;
};

#pragma pack(push, 1)
struct vector3
{
	float x, y, z;

	static std::optional<mlod_error> parse(binary_reader& reader, vector3& out)
	{
		if (!reader.read(out.x))
			return mlod_error("failed to read vector3.x");

		if (!reader.read(out.y))
			return mlod_error("failed to read vector3.y");
		
		if (!reader.read(out.z))
			return mlod_error("failed to read vector3.z");

		return {};
	}

	static void write(binary_writer& writer, const vector3& in)
	{
		writer.write(in.x);
		writer.write(in.y);
		writer.write(in.z);
	}
};

struct arma_string
{
	std::string string;
	static std::optional<mlod_error> parse(binary_reader& reader, arma_string& out)
	{
		out.string = "";
		do
		{
			char ch = '\0';
			
			if(!reader.read<char>(ch))
				return mlod_error("failed to read arma_string[n]");
			
			if (ch != '\0')
				out.string += ch;
			else break;
			
		} while (true);

		return {};
	}

	static void write(binary_writer& writer, const arma_string& in)
	{
		for (const auto& ch : in.string)
			writer.write(ch);

		writer.write('\0');
	}
};

struct vert_descriptor
{
	std::uint32_t point_index;
	std::uint32_t normal_index;
	float u;
	float v;

	static std::optional<mlod_error> parse(binary_reader& reader, vert_descriptor& out)
	{
		if(!reader.read(out.point_index))
			return mlod_error("failed to read vert_descriptor.point_index");
		if (!reader.read(out.normal_index))
			return mlod_error("failed to read vert_descriptor.normal_index");
		if (!reader.read(out.u))
			return mlod_error("failed to read vert_descriptor.u");
		if (!reader.read(out.v))
			return mlod_error("failed to read vert_descriptor.v");

		return {};
	}

	static void write(binary_writer& writer, const vert_descriptor& in)
	{
		writer.write(in.point_index);
		writer.write(in.normal_index);
		writer.write(in.u);
		writer.write(in.v);
	}
};

struct mlod_face
{
	std::uint32_t face_type{};
	std::vector<vert_descriptor> vertices;
	std::uint32_t face_flags{};
	arma_string texture_name;
	arma_string material_name;

	static std::optional<mlod_error> parse(binary_reader& reader, mlod_face& out)
	{
		if (!reader.read(out.face_type))
			return mlod_error("failed to read mlod_face.face_type");

		// always 4 nomatter the face_type
		out.vertices.resize(4);
		
		for (auto& desc : out.vertices) {
			if (!reader.read(desc))
				return mlod_error("failed to read mlod_face.vertices[n]");
		}
		
		if (!reader.read(out.face_flags))
			return mlod_error("failed to read mlod_face.face_flags");

		auto err = arma_string::parse(reader, out.texture_name);

		if (err.has_value())
			return err;
		
		err = arma_string::parse(reader, out.material_name);

		if (err.has_value())
			return err;

		return {};
	}

	static void write(binary_writer& writer, const mlod_face& in)
	{
		writer.write(in.face_type);

		for (const auto& desc : in.vertices)
			writer.write(desc);

		writer.write(in.face_flags);

		arma_string::write(writer, in.texture_name);
		arma_string::write(writer, in.material_name);
	}
	
};

struct mlod_point
{
	vector3 pos;
	std::uint32_t flags;

	static std::optional<mlod_error> parse(binary_reader& reader, mlod_point& out)
	{
		auto err = vector3::parse(reader, out.pos);

		if (err.has_value())
			return err;
		
		if (!reader.read(out.flags))
			return mlod_error("failed to read mlod_point.flags");

		return {};
	}

	static void write(binary_writer& writer, const mlod_point& in)
	{
		vector3::write(writer, in.pos);
		writer.write(in.flags);
	}
};

struct mlod_tag
{
	bool active;
	arma_string tag_name;
	std::uint32_t data_length;
	std::vector<std::uint8_t> data;

	static std::optional<mlod_error> parse(binary_reader& reader, mlod_tag& out)
	{
		if(!reader.read(out.active))
			return mlod_error("failed to read mlod_tag.active");

		auto err = arma_string::parse(reader, out.tag_name);

		if (err.has_value())
			return err;
		
		if (!reader.read(out.data_length))
			return mlod_error("failed to read mlod_tag.data_length");

		out.data.resize(out.data_length);
		
		for (auto& b : out.data) {
			if(!reader.read(b))
				return mlod_error("failed to read mlod_tag.data[m]");
		}

		return {};
	}

	static void write(binary_writer& writer, const mlod_tag& in)
	{
		writer.write(in.active);
		arma_string::write(writer, in.tag_name);
		writer.write(in.data_length);

		for (const auto& b : in.data)
			writer.write(b);
	}
};

struct property_tag
{
	std::string key{};
	std::string value{};

	property_tag() = default;
	
	explicit property_tag(const mlod_tag& parent_tag)
	{
		binary_reader reader(parent_tag.data.data(), parent_tag.data.size());
		key.resize(64);
		value.resize(64);

		for(auto& ch : key)
		{
			reader.read(ch);
		}

		for (auto& ch : value)
		{
			reader.read(ch);
		}

	}
};

struct mass_tag
{
	std::vector<float> mass;
	
	mass_tag() = default;

	explicit mass_tag(const mlod_tag& parent_tag, const std::uint32_t num_points)
	{
		binary_reader reader(parent_tag.data.data(), parent_tag.data.size());
		mass.resize(num_points);
		
		for (auto& m : mass)
		{
			reader.read(m);
		}
	}
};

struct mlod_lod
{
	mlod_signature signature{};
	std::uint32_t minor_version{};
	std::uint32_t major_version{};
	std::uint32_t num_points{};
	std::uint32_t num_face_normals{};
	std::uint32_t num_faces{};
	std::uint32_t flags{};
	std::vector<mlod_point> points;
	std::vector<vector3> normals;
	std::vector<mlod_face> faces;
	mlod_signature tag_sig{};
	std::vector<mlod_tag> tags;
	float resolution{};

	// converted tags
	std::vector<property_tag> property_tags;
	mass_tag mass;

	static std::optional<mlod_error> parse(binary_reader& reader, mlod_lod& out)
	{
		if (!reader.read(out.signature))
			return mlod_error("failed to read mlod_lod.signature");

		if (!reader.read(out.minor_version))
			return mlod_error("failed to read mlod_lod.minor_version");

		if (!reader.read(out.major_version))
			return mlod_error("failed to read mlod_lod.major_version");

		if (!reader.read(out.num_points))
			return mlod_error("failed to read mlod_lod.num_points");

		if (!reader.read(out.num_face_normals))
			return mlod_error("failed to read mlod_lod.num_face_normals");
		
		if (!reader.read(out.num_faces))
			return mlod_error("failed to read mlod_lod.num_faces");

		if (!reader.read(out.flags))
			return mlod_error("failed to read mlod_lod.flags");
		
		out.points.resize(out.num_points);
		
		for(auto& point : out.points)
		{
			auto err = mlod_point::parse(reader, point);

			if (err.has_value()) 
				return err;
		}

		out.normals.resize(out.num_face_normals);

		for(auto& normal : out.normals)
		{
			auto err = vector3::parse(reader, normal);
			
			if (err.has_value()) 
				return err;
		}

		out.faces.resize(out.num_faces);

		for(auto& face : out.faces)
		{
			auto err = mlod_face::parse(reader, face);
			
			if (err.has_value()) 
				return err;
		}

		if (!reader.read(out.tag_sig))
			return mlod_error("failed to read mlod_lod.tag_sig");
		
		do
		{
			mlod_tag tag{};
			
			auto err = mlod_tag::parse(reader, tag);

			if (err.has_value())
				return err;

			out.tags.push_back(tag);

			if(tag.tag_name.string == "#Property#")
			{
				out.property_tags.emplace_back(tag);
			}
			else if (tag.tag_name.string == "#Mass#")
			{
				out.mass = mass_tag(tag, out.num_points);
			}
			
			if (tag.tag_name.string == "#EndOfFile#") break;
			
		} while (true);

		if (!reader.read(out.resolution))
			return mlod_error("failed to read mlod_lod.resolution");

		return {};
	}

	static void write(binary_writer& writer, const mlod_lod& in)
	{
		writer.write(in.signature);
		writer.write(in.minor_version);
		writer.write(in.major_version);
		writer.write(in.num_points);
		writer.write(in.num_face_normals);
		writer.write(in.num_faces);
		writer.write(in.flags);

		for (const auto& point : in.points)
		{
			mlod_point::write(writer, point);
		}

		for (const auto& normal : in.normals)
		{
			vector3::write(writer, normal);
		}

		for (const auto& face : in.faces)
		{
			mlod_face::write(writer, face);
		}

		writer.write(in.tag_sig);

		for(const auto& tag : in.tags)
		{
			mlod_tag::write(writer, tag);
		}

		writer.write(in.resolution);
	}
};

struct p3d_header
{
	mlod_signature signature{};
	std::uint32_t version{};
	std::uint32_t lod_count{};


	static std::optional<mlod_error> parse(binary_reader& reader, p3d_header& out)
	{
		if(!reader.read<mlod_signature>(out.signature))
			return mlod_error("failed to read p3d_header.signature");

		if (!reader.read<std::uint32_t>(out.version))
			return mlod_error("failed to read p3d_header.version");

		if (!reader.read<std::uint32_t>(out.lod_count))
			return mlod_error("failed to read p3d_header.lod_count");
		
		return {};
	}

	static void write(binary_writer& writer, const p3d_header& in)
	{
		writer.write(in.signature);
		writer.write(in.version);
		writer.write(in.lod_count);
	}
};

struct mlod_p3d
{
	p3d_header header;
	std::vector<mlod_lod> lods;

	static std::optional<mlod_error> parse(binary_reader& reader, mlod_p3d& out)
	{
		auto err = p3d_header::parse(reader, out.header);

		if (err.has_value())
			return err;

		out.lods.resize(out.header.lod_count);

		for (auto& lod : out.lods)
		{
			err = mlod_lod::parse(reader, lod);

			if (err.has_value())
				return err;
		}
		return { };
	}

	static void write(binary_writer& writer, const mlod_p3d& in)
	{
		p3d_header::write(writer, in.header);

		for (const auto& lod : in.lods)
		{
			mlod_lod::write(writer, lod);
		}
	}
};

#pragma pack(pop)

int main()
{
	std::ifstream input("test.p3d", std::ios::binary);

	std::vector<char> bytes(
		(std::istreambuf_iterator<char>(input)),
		(std::istreambuf_iterator<char>()));

	input.close();

	auto* header = reinterpret_cast<p3d_header*>(bytes.data());
	auto reader = binary_reader(reinterpret_cast<std::uint8_t*>(bytes.data()), bytes.size());
	
	mlod_p3d out;
	
	auto parse_error = mlod_p3d::parse(reader, out);

	if(parse_error.has_value())
	{
		std::cout << parse_error.value().error << std::endl;
		return 1;
	}
	
	binary_writer writer;

	mlod_p3d::write(writer, out);

	auto out_file = std::fstream("test_out.p3d", std::ios::out | std::ios::binary);

	out_file.write(reinterpret_cast<char*>(writer.data.data()), writer.data.size());

	out_file.close();
	
	return 0;
}