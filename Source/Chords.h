#pragma once

#include <array>

// Arpeggiator chord presets: the chord list FL Studio offers (names and
// stacked intervals in semitones, matching FL's System/Config/Chords.map).
struct ChordPreset
{
    const char* name;
    const char* shortName;
    int group;                 // 0 triads, 1 sixths, 2 sevenths, 3 ninths, 4 elevenths, 5 thirteenths
    std::array<int, 6> steps;  // successive intervals; 0 terminates
};

inline constexpr ChordPreset chordPresets[] = {
    { "Major", "Major", 0, { 4,3 } },
    { "Suspended 2nd", "sus2", 0, { 2,5 } },
    { "Suspended 4th", "sus4", 0, { 5,2 } },
    { "Major Flat 5th", "Majb5", 0, { 4,2 } },
    { "minor", "minor", 0, { 3,4 } },
    { "Diminished", "mb5", 0, { 3,3 } },
    { "Augmented", "aug", 0, { 4,4 } },
    { "Augmented Suspended 4th", "augsus4", 0, { 5,3 } },
    { "Triton", "tri", 0, { 3,3,3 } },
    { "Sixth", "6", 1, { 4,3,2 } },
    { "Sixth Suspended 4th", "6sus4", 1, { 5,2,2 } },
    { "Sixth add 9th", "6add9", 1, { 4,3,2,5 } },
    { "Minor Sixth", "m6", 1, { 3,4,2 } },
    { "Minor Sixth 9th", "m6add9", 1, { 3,4,2,5 } },
    { "Seventh", "7", 2, { 4,3,3 } },
    { "Seventh Suspended 4th", "7sus4", 2, { 5,2,3 } },
    { "Seventh Sharp 5th", "7#5", 2, { 4,4,2 } },
    { "Seventh Flat 5th", "7b5", 2, { 4,2,4 } },
    { "Seventh Sharp 9th", "7#9", 2, { 4,3,3,5 } },
    { "Seventh Flat 9th", "7b9", 2, { 4,3,3,3 } },
    { "Seventh Sharp 5th Sharp 9th", "7#5#9", 2, { 4,4,2,5 } },
    { "Seventh Sharp 5th Flat 9th", "7#5b9", 2, { 4,4,2,3 } },
    { "Seventh Flat 5th Flat 9th", "7b5b9", 2, { 4,2,4,3 } },
    { "Seventh add 11th", "7add11", 2, { 4,3,3,7 } },
    { "Seventh add 13th", "7add13", 2, { 4,3,3,11 } },
    { "Seventh Sharp 11th", "7#11", 2, { 4,3,3,8 } },
    { "Major Seventh", "Maj7", 2, { 4,3,4 } },
    { "Major Seventh Flat 5th", "Maj7b5", 2, { 4,2,5 } },
    { "Major Seventh Sharp 5th", "Maj7#5", 2, { 4,4,3 } },
    { "Major Seventh Sharp 11th", "Maj7#11", 2, { 4,3,4,7 } },
    { "Major Seventh add 13th", "Maj7add13", 2, { 4,3,4,10 } },
    { "Minor Seventh", "m7", 2, { 3,4,3 } },
    { "Minor Seventh Flat 5th", "m7b5", 2, { 3,3,4 } },
    { "Minor Seventh Flat 9th", "m7b9", 2, { 3,4,3,3 } },
    { "Minor Seventh add 11th", "m7add11", 2, { 3,4,3,7 } },
    { "Minor Seventh add 13th", "m7add13", 2, { 3,4,3,11 } },
    { "Minor Major Seventh", "m-Maj7", 2, { 3,4,4 } },
    { "Minor Major Seventh add 11th", "m-Maj7add11", 2, { 3,4,4,6 } },
    { "Minor Major Seventh add 13th", "m-Maj7add13", 2, { 3,4,4,10 } },
    { "Ninth", "9", 3, { 4,3,3,4 } },
    { "Ninth Suspended 4th", "9sus4", 3, { 5,2,3,4 } },
    { "Ninth Add", "add9", 3, { 4,3,7 } },
    { "Ninth Sharp 5th", "9#5", 3, { 4,4,2,4 } },
    { "Ninth Flat 5th", "9b5", 3, { 4,2,4,4 } },
    { "Ninth Sharp 11th", "9#11", 3, { 4,3,3,4,4 } },
    { "Ninth Flat 13th", "9b13", 3, { 4,3,3,4,6 } },
    { "Major Ninth", "Maj9", 3, { 4,3,4,3 } },
    { "Major Ninth Suspended 4th", "Maj9sus4", 3, { 5,2,4,3 } },
    { "Major Ninth Sharp 5th", "Maj9#5", 3, { 4,4,3,3 } },
    { "Major Ninth Sharp 11th", "Maj9#11", 3, { 4,3,4,3,4 } },
    { "Minor Ninth", "m9", 3, { 3,4,3,4 } },
    { "Minor add 9th", "madd9", 3, { 3,4,7 } },
    { "Minor Ninth Flat 5th", "m9b5", 3, { 3,3,4,4 } },
    { "Minor Major Ninth", "m9-Maj7", 3, { 3,4,4,3 } },
    { "Eleventh", "11", 4, { 4,3,3,4,3 } },
    { "Eleventh Flat 9th", "11b9", 4, { 4,3,3,3,4 } },
    { "Major Eleventh", "Maj11", 4, { 4,3,4,3,3 } },
    { "Minor Eleventh", "m11", 4, { 3,4,3,4,3 } },
    { "Minor Major Eleventh", "m-Maj11", 4, { 3,4,4,3,3 } },
    { "Thirteenth", "13", 5, { 4,3,3,4,7 } },
    { "Thirteenth Sharp 9th", "13#9", 5, { 4,3,3,5,6 } },
    { "Thirteenth Flat 9th", "13b9", 5, { 4,3,3,3,8 } },
    { "Thirteenth Flat 5th Flat 9th", "13b5b9", 5, { 4,2,4,3,8 } },
    { "Major Thirteenth", "Maj13", 5, { 4,3,4,3,7 } },
    { "Minor Thirteenth", "m13", 5, { 3,4,3,4,7 } },
    { "Minor Major Thirteenth", "m-Maj13", 5, { 3,4,4,3,7 } },
};

inline constexpr int numChordPresets = (int) (sizeof (chordPresets) / sizeof (chordPresets[0]));
