#pragma once

// Utility method to see if an element is in a vector
template <class Vector, class Element> bool contains(Vector v, Element x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}
