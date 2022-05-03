#pragma once

using namespace std;

// Utility method to see if an element is in a vector
template <class Vector, class Element> bool contains(Vector v, Element x) {
    return std::find(v.begin(), v.end(), x) != v.end();

}
string label_map[2][2] = {{"CriticalVictim","MoveTo"},{"test1","test2"}};
 int map_rows =  sizeof label_map / sizeof label_map[0];
//label_map["CriticalVictim"]="MoveTo";