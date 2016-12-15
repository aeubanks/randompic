#include <iostream>
#include <cmath>
#include <array>
#include <limits>
#include <random>
#include <algorithm>
#include <sstream>

#include <boost/program_options.hpp>
#include <boost/format.hpp>

#include <X11/Xlib.h>

#define PNG_SETJMP_NOT_SUPPORTED
#include <png++/png.hpp>

namespace {

enum class dist_type {
    MANHATTAN,
    EUCLIDEAN,
    EUCLIDEAN2,
    CHEBYSHEV,
    MIN_XY,
};

std::ostream & operator<<(std::ostream & os, dist_type type) {
    switch (type) {
        case dist_type::EUCLIDEAN:
            os << "Euclidean";
            break;
        case dist_type::EUCLIDEAN2:
            os << "Euclidean2";
            break;
        case dist_type::MANHATTAN:
            os << "Manhattan";
            break;
        case dist_type::CHEBYSHEV:
            os << "Chebyshev";
            break;
        case dist_type::MIN_XY:
            os << "MinXY";
            break;
    }
    return os;
}

struct point {
    int x, y;
};

bool operator==(point a, point b) {
    return a.x == b.x && a.y == b.y;
}

bool operator!=(point a, point b) {
    return !(a == b);
}

point operator+(point a, point b) {
    return point{a.x + b.x, a.y + b.y};
}

point operator-(point a, point b) {
    return point{a.x - b.x, a.y - b.y};
}

void clamp_point(point p, int width, int height) {
    p.x %= width;
    if (p.x < 0) {
        p.x += width;
    }
    p.y %= height;
    if (p.y < 0) {
        p.y += height;
    }
}

double manhattan_dist(point d) {
    return std::abs(d.x) + std::abs(d.y);
}

double manhattan_dist(point a, point b) {
    return manhattan_dist(a - b);
}

double euclidean_dist2(point d) {
    return d.x * d.x + d.y * d.y;
}

double euclidean_dist2(point a, point b) {
    return euclidean_dist2(a - b);
}

double euclidean_dist(point d) {
    return std::sqrt(euclidean_dist2(d));
}

double euclidean_dist(point a, point b) {
    return euclidean_dist(a - b);
}

double chebyshev_dist(point d) {
    return std::max(std::abs(d.x), std::abs(d.y));
}

double chebyshev_dist(point a, point b) {
    return chebyshev_dist(a - b);
}

double min_xy_dist(point d) {
    return std::min(std::abs(d.x), std::abs(d.y));
}

double min_xy_dist(point a, point b) {
    return min_xy_dist(a - b);
}

/*
std::array<point, 4> get_corners(int width, int height) {
    return std::array<point, 4>{{point{0, 0}, point{width, 0}, point{0, height}, point{width, height}}};
}
*/

struct dist_entry {
    int width, height;
    dist_type type;
    point pnt;
    double max_dist;
    double rweight, gweight, bweight;
    bool reverse_dist;
    bool wrap;

    dist_entry(int width, int height, dist_type type, point pnt, double rweight, double gweight, double bweight, bool reverse, bool wrap): width(width), height(height), type(type), pnt(pnt), rweight(rweight), gweight(gweight), bweight(bweight), reverse_dist(reverse), wrap(wrap) {
        if (wrap) {
            max_dist = dist_delta(point{width / 2, height / 2});
        } else {
            max_dist = dist_delta(point{std::max(pnt.x, width - pnt.x), std::max(pnt.y, height - pnt.y)});
        }
    }

    double dist_delta(point d) const {
        switch (type) {
            case dist_type::EUCLIDEAN:
                return euclidean_dist(d);
            case dist_type::EUCLIDEAN2:
                return euclidean_dist2(d);
            case dist_type::MANHATTAN:
                return manhattan_dist(d);
            case dist_type::CHEBYSHEV:
                return chebyshev_dist(d);
            case dist_type::MIN_XY:
                return min_xy_dist(d);
        }
    }

    double dist_to_point(point p) const {
        point d = pnt - p;
        if (wrap) {
            if (d.x < 0) {
                d.x = std::min(-d.x, pnt.x - (p.x - width));
            } else {
                d.x = std::min(d.x, p.x + width - pnt.x);
            }
            if (d.y < 0) {
                d.y = std::min(-d.y, pnt.y - (p.y - height));
            } else {
                d.y = std::min(d.y, p.y + height - pnt.y);
            }
        }
        return dist_delta(d);
    }

    double scaled_dist(point p) const {
        double ret = dist_to_point(p) / max_dist;
        if (reverse_dist) {
            return 1.0 - ret;
        } else {
            return ret;
        }
    }
};

class pre_image {
public:
    pre_image(int width, int height): pixels(width * height), width(width), rweight(), gweight(), bweight() {}

    void add_to_weights(double rweight, double gweight, double bweight) {
        this->rweight += rweight;
        this->gweight += gweight;
        this->bweight += bweight;
    }

    void add_to_pixel(int x, int y, double r, double g, double b) {
        auto & pixel = pixel_ref(x, y);
        pixel.r += r;
        pixel.g += g;
        pixel.b += b;
    }

    png::rgb_pixel get_pixel(int x, int y) {
        auto & pixel = pixel_ref(x, y);
        double rf = rweight == 0.0 ? 0.0 : pixel.r / rweight;
        double gf = gweight == 0.0 ? 0.0 : pixel.g / gweight;
        double bf = bweight == 0.0 ? 0.0 : pixel.b / bweight;
        double scale = 3.5;
        rf *= scale;
        gf *= scale;
        bf *= scale;
        auto max = std::numeric_limits<png::byte>::max();
        auto rl = std::lround(rf * max);
        auto gl = std::lround(gf * max);
        auto bl = std::lround(bf * max);
        png::byte ri = rl;
        png::byte gi = gl;
        png::byte bi = bl;
        return {ri, gi, bi};
    }

private:
    struct pre_pixel {
        double r, g, b;
    };

    pre_pixel & pixel_ref(int x, int y) {
        return pixels[x + y * width];
    }

    std::vector<pre_pixel> pixels;

    int width;
    double rweight, gweight, bweight;
};

int rand_int_between(int lo, int hi, std::mt19937_64 & rand) {
    return std::uniform_int_distribution<>(lo, hi)(rand);
}

int rand_int_bound(int upper, std::mt19937_64 & rand) {
    return std::uniform_int_distribution<>(0, upper - 1)(rand);
}

bool rand_bool(std::mt19937_64 & rand) {
    return std::bernoulli_distribution()(rand);
}

double rand_double_between(double lo, double hi, std::mt19937_64 & rand) {
    return std::uniform_real_distribution<double>(lo, hi)(rand);
}

double rand_double(std::mt19937_64 & rand) {
    return std::uniform_real_distribution<double>()(rand);
}

dist_type rand_type(std::mt19937_64 & rand) {
    int randint = rand_int_bound(5, rand);
    return static_cast<dist_type>(randint);
}

dist_entry make_entry(int width, int height, std::mt19937_64 & rand) {
    dist_type type = rand_type(rand);
    int x = rand_int_bound(width, rand);
    int y = rand_int_bound(height, rand);
    bool reverse = rand_bool(rand);
    bool wrap = rand_bool(rand);
    double rweight = rand_double(rand);
    double gweight = rand_double(rand);
    double bweight = rand_double(rand);

    dist_entry ret{width, height, type, point{x, y}, rweight, gweight, bweight, reverse, wrap};
    return ret;
}

void set_png_bytes(png::image<png::rgb_pixel> & image, const std::vector<dist_entry> & entries) {
    int width = image.get_width();
    int height = image.get_height();

    pre_image pimage(width, height);
    for (auto & entry : entries) {
        pimage.add_to_weights(entry.rweight, entry.gweight, entry.bweight);
    }
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            point cur_point{x, y};

            for (auto & entry : entries) {
                auto dist = entry.scaled_dist(cur_point);
                double rf = dist * entry.rweight;
                double gf = dist * entry.gweight;
                double bf = dist * entry.bweight;

                pimage.add_to_pixel(x, y, rf, gf, bf);
            }
        }
    }
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            image.set_pixel(x, y, pimage.get_pixel(x, y));
        }
    }
}

void create_picture(const std::string & out_file_name, int width, int height) {
    std::mt19937_64 rand(std::random_device{}());

    int num_entries = std::poisson_distribution<>(4.0)(rand);
    if (num_entries < 2) {
        num_entries = 2;
    }
    std::vector<dist_entry> entries;
    for (int i = 0; i < num_entries; i++) {
        entries.push_back(make_entry(width, height, rand));
    }
    png::image<png::rgb_pixel> image(width, height);
    set_png_bytes(image, entries);
    image.write(out_file_name);
}

void create_video(const std::string & out_file_name, int num_frames, int width, int height) {
    std::mt19937_64 rand(std::random_device{}());

    int num_entries = std::poisson_distribution<>(4.0)(rand);
    if (num_entries < 2) {
        num_entries = 2;
    }
    num_entries = 5;
    std::vector<dist_entry> entries;
    std::vector<point> frame_pnt_delta;
    for (int i = 0; i < num_entries; i++) {
        entries.push_back(make_entry(width, height, rand));
        entries[i].wrap = true;
        point delta{0, 0};
        do {
            delta.x = rand_int_between(-2, 2, rand);
            delta.y = rand_int_between(-2, 2, rand);
        } while (delta == point{0, 0});
        frame_pnt_delta.push_back(delta);
    }
    png::image<png::rgb_pixel> image(width, height);
    for (int i = 0; i < num_frames; i++) {
        set_png_bytes(image, entries);
        auto cur_out_file_name = (boost::format(out_file_name) % i).str();
        image.write(cur_out_file_name);
        for (int j = 0; j < num_entries; j++) {
            auto & entry = entries[j];
            point new_pnt = entry.pnt + frame_pnt_delta[j];
            clamp_point(new_pnt, width, height);

            double new_rweight = entry.rweight;
            double new_gweight = entry.gweight;
            double new_bweight = entry.bweight;
            //new_rweight += rand_double_between(-0.01, 0.01, rand);
            //new_gweight += rand_double_between(-0.01, 0.01, rand);
            //new_bweight += rand_double_between(-0.01, 0.01, rand);

            entry = dist_entry(width, height, entry.type, new_pnt, new_rweight, new_gweight, new_bweight, entry.reverse_dist, entry.wrap);
        }
    }
}

std::pair<int, int> get_screen_size() {
    Display * dis = XOpenDisplay(NULL);
    Screen * screen = XDefaultScreenOfDisplay(dis);
    int width = XWidthOfScreen(screen);
    int height = XHeightOfScreen(screen);
    XCloseDisplay(dis);
    return {width, height};
}
}

int main(int argc, char ** argv) {
    auto size = get_screen_size();
    auto width = std::get<0>(size);
    auto height = std::get<1>(size);
    if (width == 0 || height == 0) {
        std::cerr << "invalid screen size: " << width << ' ' << height << '\n';
        return -1;
    }

    namespace bpo = boost::program_options;
    bpo::options_description desc("Options");
    try {
        desc.add_options()
            ("help,h", "Help description")
            ("video_frames,v", bpo::value<int>(), "Number of frames to output")
            ("output,o", bpo::value<std::string>()->required(), "Output file name (use boost::format specifiers for video_frames), can be positional argument")
            ;
        bpo::positional_options_description pos_desc;
        pos_desc.add("output", 1);
        bpo::command_line_parser parser(argc, argv);
        parser.options(desc).positional(pos_desc);
        bpo::variables_map vm;
        bpo::store(parser.run(), vm);

        if (vm.count("help")) {
            std::cout << desc << '\n';
            return 0;
        }

        bpo::notify(vm);

        std::string out_file_name = vm["output"].as<std::string>();
        if (vm.count("video_frames")) {
            int video_frames = vm["video_frames"].as<int>();
            create_video(out_file_name, video_frames, width, height);
        } else {
            create_picture(out_file_name, width, height);
        }
    } catch (bpo::error & e) {
        std::cerr << e.what() << '\n';
        std::cerr << desc << '\n';
        return 1;
    }
}

