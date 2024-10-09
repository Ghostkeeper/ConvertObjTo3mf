/*
 * Command line application to convert models to 3MF.
 * Copyright (C) 2020 Ghostkeeper
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for details.
 * You should have received a copy of the GNU Affero General Public License along with this library. If not, see <https://gnu.org/licenses/>.
 */

#include <fstream> //To read binary STL files.
#include <iostream> //To message progress.
#include <array>

#include "stl_binary.hpp" //The definitions for this file.

namespace convertto3mf {

float StlBinary::is_stl_binary(const std::string& filename) {
	float probability = 1.0 / 3.0; //Final result.
	//Probability of a file extension being different from the contents of the file. Probably an overestimation but we want to let the magic number determine it more.
	constexpr float probability_incorrect_extension = 0.01;
	//Probability of the file having an inconsistent file size. Binary STL files should normally be 84+50*N bytes large. N is stored in bytes 80-83 of the header.
	constexpr float probability_incorrect_size = 0.0001;

	//File extension plays a role in likelihood.
	if(filename.find_last_of(".stl") == filename.length() - 4) { //Ends in .stl
		probability = 1 - probability_incorrect_extension;
	} else {
		probability = probability_incorrect_extension;
	}

	bool correct_file_size = true;
	//Check that the file is at least as big as the minimum header.
	std::ifstream file_handle(filename, std::ios_base::in | std::ios_base::binary);
	file_handle.seekg(0, file_handle.end);
	size_t file_size = file_handle.tellg();
	file_handle.seekg(file_handle.beg);
	if(file_size < 84) {
		correct_file_size = false;
	}

	//Read the supposed number of triangles.
	file_handle.seekg(80, file_handle.beg);
	uint32_t num_triangles;
	file_handle.read((char*)&num_triangles, sizeof(num_triangles)); //Works correctly since most CPUs are little-endian.

	//Verify that the file size is exactly correct.
	if(file_size != 84 + 50 * num_triangles) {
		correct_file_size = false;
	}

	//Adjust the probability based on the file size.
	if(correct_file_size) {
		probability = 1.0 - ((1.0 - probability) * probability_incorrect_size);
	} else {
		probability *= probability_incorrect_size;
	}

	return probability;
}

Model StlBinary::import(const std::string& filename) {
	std::cout << "Importing binary STL file: " << filename << std::endl;
	StlBinary stl; //Store the STL in its own representation.

	stl.load(filename);
	return stl.to_model();
}

void StlBinary::load(const std::string& filename) {
	std::ifstream file_handle(filename, std::ios_base::in | std::ios_base::binary);

	//Read the number of triangles.
	file_handle.seekg(80, file_handle.beg);
	uint32_t num_triangles;
	file_handle.read((char*)&num_triangles, sizeof(num_triangles));
	file_handle.seekg(0, file_handle.end);
	size_t file_size = file_handle.tellg();
	file_handle.seekg(84, file_handle.beg);
	if((file_size - 84) / 50 < num_triangles) { //Number of triangles must be corrupt.
		num_triangles = (file_size - 84) / 50; //Prevent reading outside of the file, or allocating absurd amounts of memory.
	}
	triangles.reserve(num_triangles);

	for(size_t triangle_index = 0; triangle_index < num_triangles; ++triangle_index) {
		file_handle.seekg(84 + triangle_index * 50 + 12); //Skip over the normal vector. We don't need them.
		float v1_x, v1_y, v1_z, v2_x, v2_y, v2_z, v3_x, v3_y, v3_z;
		file_handle.read((char*)&v1_x, sizeof(float)); //Assuming that your CPU uses 32-bit floats, which is pretty much every desktop CPU.
		file_handle.read((char*)&v1_y, sizeof(float));
		file_handle.read((char*)&v1_z, sizeof(float));
		file_handle.read((char*)&v2_x, sizeof(float));
		file_handle.read((char*)&v2_y, sizeof(float));
		file_handle.read((char*)&v2_z, sizeof(float));
		file_handle.read((char*)&v3_x, sizeof(float));
		file_handle.read((char*)&v3_y, sizeof(float));
		file_handle.read((char*)&v3_z, sizeof(float));

		std::array<Point3, 3> triangle = {
			Point3(v1_x, v1_y, v1_z),
			Point3(v2_x, v2_y, v2_z),
			Point3(v3_x, v3_y, v3_z)
		};
		triangles.push_back(triangle);
	}
}

Model StlBinary::to_model() const {
	Model model; //The result.
	model.meshes.emplace_back(); //There's always just one mesh in binary STLs.
	Mesh& mesh = model.meshes.back();

	for(const std::array<Point3, 3>& triangle : triangles) {
		mesh.faces.emplace_back();
		Face& face = mesh.faces.back();
		face.vertices.push_back(triangle[0]);
		face.vertices.push_back(triangle[1]);
		face.vertices.push_back(triangle[2]);
	}

	return model;
}

}