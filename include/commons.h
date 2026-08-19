#ifndef HASHMAPS_COMMONS_H
#define HASHMAPS_COMMONS_H

#define SIPHASH_2_4_KEY_SIZE 16

typedef enum Option {
    None,
    Some,
} Option;

typedef struct element {
    // Owned by the hashmap, do not free or modify this pointer
    const char* key;
    int value;
} Element;

// Optional containing an Element stored by value
// The key pointer is still owned by the hashmap
typedef struct Option_Element_v {
    Option option;
    Element element_v;
} Option_Element_v;

// Optional containing an Element stored as a pointer
// The pointer is invalid after destroy and may be invalid after resize
typedef struct Option_Element_p {
    Option option;
    Element* element_p;
} Option_Element_p;

#endif
