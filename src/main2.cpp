#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <string>
#include <optional>

#include "stb/stb_image.h"

#define eprintf(...) std::fprintf(stderr, __VA_ARGS__)

/**
 * A single color, holding values for four channels: red, green, blue, and alpha
 */
struct ImagePixel {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
    std::uint8_t alpha;
};

struct Monochrome {
    std::uint8_t intensity;

    Monochrome() = default;
    Monochrome(const Monochrome &) = default;
    Monochrome(const ImagePixel &pixel) {
        // Based on Luma
        static const std::uint8_t RED_WEIGHT = 0x90;
        static const std::uint8_t GREEN_WEIGHT = 0x0F;
        static const std::uint8_t BLUE_WEIGHT = 0x60;
        static_assert(RED_WEIGHT + GREEN_WEIGHT + BLUE_WEIGHT == UINT8_MAX, "red + green + blue weights (grayscale) must equal 255");

        this->intensity = 0;
        #define COMPOSITE_BITMAP_GRAYSCALE_SET_CHANNEL_VALUE_FOR_COLOR(channel, weight) \
            this->intensity += (pixel.channel * weight + (UINT8_MAX + 1) / 2) / UINT8_MAX;

        COMPOSITE_BITMAP_GRAYSCALE_SET_CHANNEL_VALUE_FOR_COLOR(red, RED_WEIGHT)
        COMPOSITE_BITMAP_GRAYSCALE_SET_CHANNEL_VALUE_FOR_COLOR(green, GREEN_WEIGHT)
        COMPOSITE_BITMAP_GRAYSCALE_SET_CHANNEL_VALUE_FOR_COLOR(blue, BLUE_WEIGHT)

        #undef COMPOSITE_BITMAP_GRAYSCALE_SET_CHANNEL_VALUE_FOR_COLOR
    }

    operator std::uint8_t &() {
        return this->intensity;
    }

    void operator=(const std::uint8_t &new_intensity) {
        this->intensity = new_intensity;
    }
};

static std::vector<ImagePixel> load_image(const char *path, std::uint32_t &image_width, std::uint32_t &image_height) {
    // Load it
    int x = 0, y = 0, channels = 0;
    auto *image_buffer = reinterpret_cast<ImagePixel *>(stbi_load(path, &x, &y, &channels, 4));
    if(!image_buffer) {
        eprintf("Failed to load %s. Error was: %s\n", path, stbi_failure_reason());
        exit(EXIT_FAILURE);
    }

    // Get the width and height
    image_width = static_cast<std::uint32_t>(x);
    image_height = static_cast<std::uint32_t>(y);
    std::vector<ImagePixel> return_value(image_buffer, image_buffer + image_width * image_height);

    // Free the buffer
    stbi_image_free(image_buffer);

    return return_value;
}

struct TagReflexive {
    std::uint32_t count;
    std::uint32_t reserved[2];
};

struct TagDataOffset {
    std::uint32_t count;
    std::uint32_t reserved[4];
};

template<typename T> inline T swap_endian(const T &value) {
    std::byte c[sizeof(T)];
    const std::byte *cr = reinterpret_cast<const std::byte *>(&value);
    for(std::size_t i = 0; i < sizeof(T); i++) {
        c[i] = cr[sizeof(T) - 1 - i];
    }
    return *reinterpret_cast<T *>(c);
}

struct Font {
    std::int32_t flags;
    std::int16_t ascending_height;
    std::int16_t descending_height;
    std::int16_t leading_height;
    std::int16_t leading_width;
    char padding[0x24];
    TagReflexive character_tables;
    char bold[0x10]; // font
    char italic[0x10]; // font
    char condense[0x10]; // font
    char underline[0x10]; // font
    TagReflexive characters;
    TagDataOffset pixels;
};
static_assert(sizeof(Font) == 0x9C);

struct FontCharacter {
    std::int16_t character;
    std::int16_t character_width;
    std::int16_t bitmap_width;
    std::int16_t bitmap_height;
    std::int16_t bitmap_origin_x;
    std::int16_t bitmap_origin_y;
    std::int16_t hardware_character_index;
    char padding[0x2];
    std::uint32_t pixels_offset;
};
static_assert(sizeof(FontCharacter) == 0x14);

template<typename T> struct Image {
    std::uint32_t width;
    std::uint32_t height;
    std::vector<T> pixels;
    std::string text;
};

using MonochromeImage = Image<Monochrome>;

/*MonochromeImage draw_character(char c, const std::vector<Monochrome> &monochrome_pixels, const std::vector<FontCharacter> &characters, const Font &font) {
    const FontCharacter &character = characters[c];
    MonochromeImage output;

    output.height = font.ascending_height + font.descending_height;
    output.width = character.character_width;
}*/

MonochromeImage draw_text(const char *text, const std::vector<Monochrome> &monochrome_pixels, const std::vector<FontCharacter> &characters, const Font &font) {
    std::vector<FontCharacter> characters_to_draw;
    for(const char *c = text; *c; c++) {
        characters_to_draw.push_back(characters[static_cast<std::uint8_t>(*c)]);
    }

    // Get ready to draw some pixels
    MonochromeImage drawn_text = {};
    drawn_text.text = text;
    drawn_text.height = swap_endian(font.ascending_height) + swap_endian(font.descending_height);
    for(auto &c : characters_to_draw) {
        drawn_text.width += swap_endian(c.character_width);
    }
    drawn_text.pixels.insert(drawn_text.pixels.begin(), drawn_text.height * drawn_text.width, Monochrome());

    std::uint32_t x_cursor = 0;

    // Draw those pixels
    for(auto &c : characters_to_draw) {
        auto bitmap_width = swap_endian(c.bitmap_width);
        auto bitmap_height = swap_endian(c.bitmap_height);

        auto bitmap_y = swap_endian(font.ascending_height) - swap_endian(c.bitmap_origin_y);

        if(bitmap_width > 0 && bitmap_height > 0) {
            auto *pixels = monochrome_pixels.data() + swap_endian(c.pixels_offset);

            auto set_pixel = [&drawn_text](std::uint32_t x, std::uint32_t y, const Monochrome &pixel) {
                if(drawn_text.width > x && drawn_text.height > y) {
                    auto &output_pixel = drawn_text.pixels[x + y * drawn_text.width];
                    output_pixel = static_cast<std::uint8_t>(static_cast<std::uint32_t>(pixel.intensity) * 3 / 4);
                }
            };

            for(std::uint32_t y = 0; y < bitmap_height; y++) {
                for(std::uint32_t x = 0; x < bitmap_width; x++) {
                    set_pixel(x_cursor + x, y + bitmap_y, pixels[x + y * bitmap_width]);
                }
            }
        }
        x_cursor += swap_endian(c.character_width);
    }

    return drawn_text;
}

void filter_monochrome(std::vector<Monochrome> &monochrome_data) {
    for(auto &m : monochrome_data) {
        static constexpr std::uint8_t MINIMUM = 0x4F;
        if(m < MINIMUM) {
            m = 0;
        }
        else {
            m = 0xFF;
        }
    }
}

int main(int argc, const char **argv) {
    if(argc != 3) {
        eprintf("Usage: %s <image> <font>\n", argv[0]);
        return EXIT_FAILURE;
    }

    std::uint32_t width, height;
    auto image_data = load_image(argv[1], width, height);

    if(width != width && height != 480) {
        eprintf("Cannot support non-widthx480 images right now...\n");
        return EXIT_FAILURE;
    }

    // Convert to monochrome
    std::vector<Monochrome> monochrome_version(image_data.begin(), image_data.end());
    filter_monochrome(monochrome_version);

    std::FILE *m = std::fopen("mono.data", "wb");
    std::fwrite(monochrome_version.data(), width, 480, m);
    std::fclose(m);

    // Load the font tag
    std::FILE *f = std::fopen(argv[2], "rb");
    if(!f) {
        eprintf("Could not open font tag %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    // First, seek to the font tag header
    std::fseek(f, 0x40, SEEK_SET);

    // Read the data
    Font font;
    std::fread(&font, sizeof(font), 1, f);

    // Skip that shit
    auto character_tables_count = swap_endian(font.character_tables.count);
    if(character_tables_count) {
        std::vector<TagReflexive> tables(character_tables_count);
        std::fread(tables.data(), character_tables_count * 0xC, 1, f);

        for(auto &table : tables) {
            std::fseek(f, 2 * swap_endian(table.count), SEEK_CUR);
        }
    }

    // Read characters
    std::vector<FontCharacter> characters(256);
    auto character_count = swap_endian(font.characters.count);

    std::vector<FontCharacter> all_characters(character_count);
    std::fread(all_characters.data(), character_count * sizeof(FontCharacter), 1, f);

    for(auto &c : all_characters) {
        auto character_index = swap_endian(c.character);
        if(character_index < 256 && character_index > 0) {
            characters[character_index] = c;
        }
    }

    // Lastly, finish the thing
    std::uint32_t pixel_size = swap_endian(font.pixels.count);
    std::vector<Monochrome> pixels(pixel_size);
    std::fread(pixels.data(), pixel_size, 1, f);
    std::fclose(f);

    auto match = [&monochrome_version](const MonochromeImage &text, std::uint32_t x, std::uint32_t y) -> float {
        if(x + text.width > width || y + text.height > 480 || text.width == 0 || text.height == 0) {
            return 0.0F;
        }

        std::uint32_t hits = 0;
        std::uint32_t total = text.height * text.width;

        for(std::uint32_t ty = 0; ty < text.height; ty++) {
            for(std::uint32_t tx = 0; tx < text.width; tx++) {
                const auto &text_pixel = text.pixels[tx + ty * text.width];
                const auto &image_pixel = monochrome_version[tx + x + (ty + y) * width];

                std::int32_t difference = static_cast<std::int32_t>(text_pixel.intensity) - image_pixel.intensity;
                if(difference < 0) {
                    difference *= -1;
                }

                hits += (difference < 0x10);
            }
        }

        return static_cast<float>(hits) / total;
    };

    std::uint32_t name_x, name_y;
    std::uint32_t score_x, score_y;
    std::uint32_t kills_x, kills_y;
    std::uint32_t assists_x, assists_y;
    std::uint32_t deaths_x, deaths_y;

    auto line_height_search = swap_endian(font.ascending_height);

    auto find_header_text = [&pixels, &characters, &font, &match, &line_height_search](const char *text, std::uint32_t min_x, std::uint32_t min_y, std::uint32_t &found_x, std::uint32_t &found_y) {
        auto text_drawn = draw_text(text, pixels, characters, font);
        filter_monochrome(text_drawn.pixels);

        float found_percent = 0.0F;

        for(std::uint32_t y = min_y; y < min_y + line_height_search; y++) {
            for(std::uint32_t x = min_x; x < width; x++) {
                float match_percent = match(text_drawn, x, y);
                if(match_percent > found_percent) {
                    found_percent = match_percent;
                    found_x = x;
                    found_y = y;
                }
            }
        }

        if(found_percent < 0.85) {
            eprintf("Failed to find \"%s\". Best guess was %u,%u, but we only got a %f%% match.\n", text, found_x, found_y, found_percent * 100.0F);
            std::exit(EXIT_FAILURE);
        }
    };

    find_header_text("Name", 120, 120, name_x, name_y);
    find_header_text("Score", name_x, name_y - 10, score_x, score_y);
    find_header_text("Kills", score_x, name_y - 10, kills_x, kills_y);
    find_header_text("Assists", kills_x, name_y - 10, assists_x, assists_y);
    find_header_text("Deaths", assists_x, name_y - 10, deaths_x, deaths_y);

    std::uint32_t y_cursor = name_y;

    // Skip to the next line
    auto skip_to_next_line = [&y_cursor, &monochrome_version, &line_height_search, &deaths_x]() {
        y_cursor += line_height_search / 2;

        while(y_cursor < 480) {
            bool should_break = true;

            // See if there's anything this line. Checking deaths is fastest since it is rightmost
            for(std::uint32_t x = deaths_x; x < width; x++) {
                if(monochrome_version[x + y_cursor * width]) {
                    should_break = false;
                    break;
                }
            }
            if(should_break) {
                break;
            }
            y_cursor++;
        }
    };

    skip_to_next_line();

    struct PlayerStats {
        bool red;
        std::string name;
        std::int8_t score;
        std::int8_t kills;
        std::int8_t assists;
        std::int8_t deaths;
    };

    std::vector<PlayerStats> players;

    // Generate some numbers to look for
    std::vector<MonochromeImage> numbers(10);
    for(std::size_t i = 0; i < numbers.size(); i++) {
        char v[2] = {};
        v[0] = static_cast<char>(i) + '0';
        numbers[i] = draw_text(v, pixels, characters, font);
        filter_monochrome(numbers[i].pixels);
    }
    filter_monochrome(numbers.emplace_back(draw_text("-", pixels, characters, font)).pixels);

    std::vector<MonochromeImage> all;
    for(std::size_t i = 0; i < characters.size(); i++) {
        if(characters[i].character_width && (i == ' ' || (i > 'a' && i < 'z') || (i > 'A' && i < 'Z') || (i > '0' && i < '9'))) {
            char v[2] = {};
            v[0] = static_cast<char>(i);
            filter_monochrome(all.emplace_back(draw_text(v, pixels, characters, font)).pixels);
        }
    }

    // Go through each line
    while(true) {
        // See if there's something on this line. Checking deaths is fastest since it's the rightmost
        bool found_something = false;
        for(std::uint32_t y = y_cursor; y < y_cursor + line_height_search && !found_something; y++) {
            for(std::uint32_t x = deaths_x; x < width && !found_something; x++) {
                found_something = monochrome_version[x + y * width].intensity > 0;
            }
        }

        if(!found_something) {
            break;
        }

        // Let's get some numbers
        auto string_at = [&match, &line_height_search, &monochrome_version, &image_data](std::uint32_t search_x, std::uint32_t search_y, std::uint32_t end_x, const std::vector<MonochromeImage> &table, bool *red = nullptr) -> std::string {
            std::uint32_t x = search_x;

            // Get the length of the string
            std::uint32_t max_x;
            std::uint32_t drought = 0;
            for(max_x = x + 1; max_x < end_x; max_x++) {
                drought++;
                for(std::uint32_t y = search_y + 4; y < search_y + line_height_search; y++) {
                    if(monochrome_version[max_x + y * width].intensity) {
                        drought = 0;
                        break;
                    }
                }
            }

            // Add up characters until we finish up
            std::string final_string;
            while(x < max_x) {
                float best_character_percent = 0.0F;
                char best_character;
                std::optional<std::uint32_t> best_length;
                std::uint32_t best_x;

                // Give some leeway for a few pixels
                for(std::int32_t my = -3; my < 4; my++) {
                    for(std::int32_t mx = -3; mx < 4; mx++) {
                        for(auto &c : table) {
                            if(x + c.width * 1.5F > max_x) {
                                continue;
                            }

                            float test = match(c, x + mx, search_y + my);
                            if(test > best_character_percent) {
                                best_character_percent = test;
                                best_character = c.text[0];
                                best_length = c.width;
                                best_x = mx;
                            }
                        }
                    }
                }

                if(!best_length.has_value()) {
                    break;
                }

                x += best_length.value();
                final_string += best_character;
            }

            // Determine if it was red or blue
            if(red) {
                bool found = false;
                for(std::uint32_t y = search_y; y < search_y + line_height_search; y++) {
                    for(std::uint32_t x = search_x; x < max_x && !found; x++) {
                        if(monochrome_version[x + width * y].intensity > 0x7F) {
                            auto &pixel = image_data[x + width * y];
                            if(Monochrome(pixel).intensity > 0x7F) {
                                *red = pixel.red > pixel.blue;
                                found = true;
                                break;
                            }
                        }
                    }
                }
            }

            return final_string;

            //return final_string.substr(0, final_string.size() - 2);

        };

        /*player.score = string_at(score_x, y_cursor, false);
        player.kills = string_at(kills_x, y_cursor, false);
        player.assists = string_at(assists_x, y_cursor, false);
        player.deaths = string_at(deaths_x, y_cursor, false);*/

        auto &player = players.emplace_back();
        player.name = string_at(name_x, y_cursor, score_x, all, &player.red);
        player.score = std::strtol(string_at(score_x, y_cursor, kills_x, numbers).data(), nullptr, 10);
        player.kills = std::strtol(string_at(kills_x, y_cursor, assists_x, numbers).data(), nullptr, 10);
        player.assists = std::strtol(string_at(assists_x, y_cursor, deaths_x, numbers).data(), nullptr, 10);
        player.deaths = std::strtol(string_at(deaths_x, y_cursor, width, numbers).data(), nullptr, 10);

        skip_to_next_line();
    }

    eprintf("\n");
    eprintf("Name                 | Team | Score | Kills | Assists | Deaths\n");
    eprintf("---------------------|------|-------|-------|---------|--------\n");
    bool ffa = true;
    std::int32_t red_score = 0;
    std::int32_t blue_score = 0;
    for(std::size_t i = 0; i < players.size(); i++) {
        if(players[i].red) {
            ffa = false;
            red_score += players[i].score;
        }
        else {
            blue_score += players[i].score;
        }
        eprintf("%-20s | %-4s | %5i | %5i | %7i | %6i\n", players[i].name.data(), players[i].red ? "Red" : "Blue", players[i].score, players[i].kills, players[i].assists, players[i].deaths);
    }
    eprintf("\n");
    if(!ffa) {
        std::int32_t winning_score;
        std::int32_t losing_score;
        const char *result_text;

        if(red_score > blue_score) {
            result_text = "Red team wins";
            winning_score = red_score;
            losing_score = blue_score;
        }
        else if(blue_score > red_score) {
            result_text = "Blue team wins";
            losing_score = red_score;
            winning_score = blue_score;
        }
        else {
            result_text = "Teams are tied";
            losing_score = red_score;
            winning_score = blue_score;
        }

        eprintf("Final score: %s %i - %i.\n\n", result_text, winning_score, losing_score);
    }




    // Now, look for the name.

    //f = std::fopen("hello_world.data", "wb");
    //std::fwrite(text.pixels.data(), text.height * text.width, 1, f);
    //std::fclose(f);
}
