/*
 * Command line application to convert models to 3MF.
 * Copyright (C) 2020 Ghostkeeper
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for details.
 * You should have received a copy of the GNU Affero General Public License along with this library. If not, see <https://gnu.org/licenses/>.
 */

#include <iostream> //To give progress updates.
#include <fstream> //To read the ASCII STL files.
#include <regex> //To match with the syntax of STL to detect the file format.

#include "stl_ascii.hpp" //Definitions for this file.

namespace convertto3mf {

float StlAscii::is_stl_ascii(const std::string& filename) {
	float probability = 1.0 / 3.0; //Final result.
	//Probability of a file extension being different from the contents of the file. Probably an overestimation but we want to let the magic number determine it more.
	constexpr float probability_incorrect_extension = 0.01;
	//Probability of a line being inconsistent with its file type, like there being "solid" in the file if it's not ASCII STL, or gibberish even though it is ASCII STL.
	constexpr float probability_incorrect_line = 0.01;

	//File extension plays a role in likelihood.
	if(filename.find_last_of(".stl") == filename.length() - 4) { //Ends in .stl
		probability = 1 - probability_incorrect_extension;
	} else {
		probability = probability_incorrect_extension;
	}

	//Read 1kB from the file to see if the file appears to be correctly formatted for ASCII STL.
	std::ifstream file_handle(filename);
	file_handle.seekg(0, file_handle.end);
	size_t sample_size = file_handle.tellg();
	file_handle.seekg(file_handle.beg);
	sample_size = std::min(sample_size, size_t(1024)); //Limit to 1kB.
	std::vector<char> buffer(sample_size);
	file_handle.read(&buffer.front(), sample_size);
	std::string sample(buffer.data(), buffer.size()); //We've read up to 1kB from this file now.

	//Match a line with this regex that matches on the possible lines of ASCII STL.
	const std::regex correct_line("^\\s*$|\\s*solid.*|\\s*facet\\s*|\\s*facet normal .*|\\s*outer loop\\s*|\\s*vertex .*|\\s*endloop\\s*|\\s*endfacet\\s*|\\s*endsolid.*");
	size_t correct_lines = 0;
	size_t lines_found = 0;
	size_t line_start = 0;
	size_t line_end = 0;
	while((line_end = sample.find('\n', line_start)) != sample.npos) {
		std::string line = sample.substr(line_start, line_end - line_start);
		if(line.find('\r') == line.length() - 1) { //Remove carriage returns if it's in this file.
			line = line.substr(0, line.length() - 1);
		}
		lines_found++;
		if(std::regex_match(line, correct_line)) {
			correct_lines++;
		}
		line_start = line_end + 1;
	}
	for(size_t i = 0; i < lines_found; ++i) {
		if(i < correct_lines) { //This line was correct.
			probability = 1.0 - ((1.0 - probability) * probability_incorrect_line);
		} else { //This line was incorrect.
			probability *= probability_incorrect_line;
		}
	}

	return probability;
}

Model StlAscii::import(const std::string& filename) {
	std::cout << "Importing ASCII STL file: " << filename << std::endl;
	StlAscii stl; //Store the STL in its own representation.

	stl.load(filename);
	return stl.to_model();
}

void StlAscii::load(const std::string& filename) {
	std::ifstream file_handle(filename);

	//Get all lines from the file and trim them.
	std::vector<std::string> lines;
	lines.reserve(128000); //Most files are going to contain at least this amount of lines. Prevent copying too often when growing.
	for(std::string line; std::getline(file_handle, line);) {
		//Trim whitespace from the line.
		size_t first = line.find_first_not_of(" \t\n\r\f");
		if(first == std::string::npos) {
			line = "";
		} else {
			size_t last = line.find_last_not_of(" \t\n\r\f");
			line = line.substr(first, (last - first + 1));
		}
		lines.push_back(line);
	}

	std::vector<std::vector<Point3>>* mesh = nullptr; //The current mesh we're working on, or nullptr if we're not currently parsing a mesh.
	std::vector<Point3>* face = nullptr; //The current face we're working on, or nullptr if we're not currently parsing a facet.
	bool in_loop = false; //Track whether we're currently inside of an "outer loop" definition. Vertices are only valid inside the loop.

	for(const std::string& line : lines) {
		if(line.find("solid") == 0) {
			meshes.emplace_back(); //Invalidates pointers! Make sure we reset those.
			mesh = &meshes.back();
			face = nullptr;
			in_loop = false;
		} else if(line.find("endsolid") == 0) {
			mesh = nullptr;
			face = nullptr;
			in_loop = false;
		} else if(line.find("facet") == 0 && mesh) { //Ignoring the normal vector here. We don't store that information.
			mesh->emplace_back();
			face = &mesh->back();
			in_loop = false;
		} else if(line == "endfacet") {
			face = nullptr;
			in_loop = false;
		} else if(line == "outer loop" && mesh && face) {
			in_loop = true;
		} else if(line == "endloop") {
			in_loop = false;
		} else if(line.find("vertex") == 0 && mesh && face && in_loop) {
			//Parse the vertex coordinates!
			size_t x_start = line.find_first_not_of(' ', 6); //First non-space after the "vertex" keyword.
			if(x_start == std::string::npos) {
				continue; //The line was just "vertex     " with a number of spaces but no X coordinate.
			}
			size_t x_end = line.find(' ', x_start);
			if(x_end == std::string::npos) {
				continue; //There was no space after the X coordinate, meaning that there is no Y coordinate.
			}
			size_t y_start = line.find_first_not_of(' ', x_end);
			if(y_start == std::string::npos) {
				continue; //There were just a number of spaces after the X coordinate, no Y coordinate.
			}
			size_t y_end = line.find(' ', y_start);
			if(y_end == std::string::npos) {
				continue; //There was no space after the Y coordinate, meaning that there is no Z coordinate.
			}
			size_t z_start = line.find_first_not_of(' ', y_end);
			if(z_start == std::string::npos) {
				continue; //There were just a number of spaces after the Y coordinate, no Z coordinate.
			}
			size_t z_end = line.find(' ', z_start);
			if(z_end == std::string::npos) {
				z_end = line.length(); //No spaces after the Z coordinate. That's all right though, just cut it off at the end of the string.
			}

			const std::string x_str = line.substr(x_start, x_end - x_start);
			const std::string y_str = line.substr(y_start, y_end - y_start);
			const std::string z_str = line.substr(z_start, z_end - z_start);

			//Convert everything to our coordinate type.
			char* end;
			const coord_t x = strtod(x_str.c_str(), &end);
			if(*end) { //Not a number.
				continue;
			}
			const coord_t y = strtod(y_str.c_str(), &end);
			if(*end) {
				continue;
			}
			const coord_t z = strtod(z_str.c_str(), &end);
			if(*end) {
				continue;
			}

			//Successfully parsed a vertex! Store it in our face.
			face->emplace_back(x, y, z);
		}
	}
}

Model StlAscii::to_model() const {
	Model model; //The result.

	//Basically a one-on-one copy from our data structure into the common one.
	for(const std::vector<std::vector<Point3>>& my_mesh : meshes) {
		model.meshes.emplace_back();
		Mesh& mesh = model.meshes.back();
		for(const std::vector<Point3>& my_face : my_mesh) {
			mesh.faces.emplace_back();
			Face& face = mesh.faces.back();
			for(const Point3& my_vertex : my_face) {
				face.vertices.push_back(my_vertex);
			}
		}
	}

	return model;
}

}