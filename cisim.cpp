#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <string>
#include <map>
#include <vector>
#include <fstream>

namespace fs = std::filesystem;

std::string lowerstring(const std::string& str)
{
    std::string lowerstr = str;
    std::transform(
        lowerstr.begin(),
        lowerstr.end(),
        lowerstr.begin(),
        [](unsigned char c){ return std::tolower(c); }
    );
    return lowerstr;
}

class RealNames
{
private:
    std::map<std::string, fs::path> realnames;
public:
    RealNames(const char* dir)
    {
        for (const fs::directory_entry& de : fs::recursive_directory_iterator(dir))
        {
            if (!de.is_symlink())
            {
                const fs::path path = fs::canonical(de.path());
                realnames.emplace(lowerstring(path.string()), path);
            }
        }
    }

    bool realNameRegistered(const fs::path& path) const
    {
        return realnames.contains(lowerstring((fs::canonical(path.parent_path()) / path.filename()).string()));
    }

    const fs::path& realName(const fs::path& path) const
    {
        return realnames.at(lowerstring((fs::canonical(path.parent_path()) / path.filename()).string()));
    }

    void createSymlinkIfNecessary(const fs::path& referredToAs) const
    {
        try
        {
            if (realNameRegistered(referredToAs))
            {
                const fs::path& rn = realName(referredToAs);
                
                if (!fs::exists(referredToAs))
                {
                    std::printf("%s <- %s\n", rn.string().c_str(), referredToAs.string().c_str());
                    if (fs::is_directory(rn))
                    {
                        fs::create_directory_symlink(
                            rn,
                            referredToAs
                        );
                    }
                    else
                    {
                        fs::create_symlink(
                            rn,
                            referredToAs
                        );
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            std::printf("Ignorerar fel: %s\n", e.what());
        }
        catch (...)
        {
            std::puts("Ignorerar fel: Okänt fel");
        }
    }
};

enum struct ImportType
{
    fail = 0,
    quotes,
    angles
};

class Parser
{
private:
    std::vector<char> data;
    const char* cursor;

    void skipAllWhitespace()
    {
        while (cursor != &*data.end() && isspace(*cursor))
            cursor++;
    }

    void skipNonlinebreakWhitespace()
    {
        while (cursor != &*data.end() && isspace(*cursor) && *cursor != '\n' && *cursor != '\r')
            cursor++;
    }

    bool tryParse(const char* str)
    {
        const char* tmpcursor = cursor;
        while (*str != '\0')
        {
            if (tmpcursor == &*data.end() || *tmpcursor++ != *str++)
            {
                return false;
            }
        }
        cursor = tmpcursor;
        return true;
    }

    bool tryReadImportFileName(std::string& out, char endChar)
    {
        while (cursor != &*data.end() && *cursor != '\n' && *cursor != '\r' && *cursor != endChar)
            out += *cursor++;
        return *cursor == endChar;
    }

    void skipUntilNextLine()
    {
        while (cursor != &*data.end() && *cursor != '\n' && *cursor != '\r')
            cursor++;
    }
public:
    Parser(const fs::path& file)
    {
        std::ifstream ifs(file, std::ios::in | std::ios::binary);
        ifs.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        data.resize(fs::file_size(file));
        ifs.read((char*)data.data(), data.size());
        cursor = &data[0];
    }

    ImportType tryGetImport(std::string& out)
    {
        skipAllWhitespace();
        if (!tryParse("#")) return skipUntilNextLine(), ImportType::fail;
        skipNonlinebreakWhitespace();
        if (!tryParse("include")) return skipUntilNextLine(), ImportType::fail;
        skipNonlinebreakWhitespace();
        if (tryParse("\""))
        {
            if (!tryReadImportFileName(out, '"')) return skipUntilNextLine(), ImportType::fail;
            if (!tryParse("\"")) return skipUntilNextLine(), ImportType::fail;
            return ImportType::quotes;
        }
        else if (tryParse("<"))
        {
            if (!tryReadImportFileName(out, '>')) return skipUntilNextLine(), ImportType::fail;
            if (!tryParse(">")) return skipUntilNextLine(), ImportType::fail;
            return ImportType::angles;
        }
        return skipUntilNextLine(), ImportType::fail;
    }

    bool atEnd() const
    {
        return cursor == &*data.end();
    }
};

bool isCpp(const std::string& extension)
{
    return
        extension == ".c" ||
        extension == ".cc" ||
        extension == ".cpp" ||
        extension == ".cxx" ||
        extension == ".c++" ||
        extension == ".h" ||
        extension == ".hh" ||
        extension == ".hpp" ||
        extension == ".hxx" ||
        extension == ".h++";
}

int main(int argc, char* argv[])
{
    try
    {
        if (argc != 2)
        {
            std::puts("Användning: cisim [mapp]");
            return 1;
        }

        std::vector<fs::path> libpaths;
        for (const fs::directory_entry& de : fs::directory_iterator(argv[1]))
        {
            libpaths.push_back(fs::canonical(de.path()));
            std::puts(de.path().string().c_str());
        }

        const RealNames realNames(argv[1]);
        for (const fs::directory_entry& de : fs::recursive_directory_iterator(argv[1]))
        {
            const fs::path canonicalpath = fs::canonical(de.path());
            realNames.createSymlinkIfNecessary(lowerstring(canonicalpath.string()));
            if (fs::is_directory(canonicalpath) || !isCpp(canonicalpath.extension().string()))
                continue;
            Parser p(canonicalpath);
            while (!p.atEnd())
            {
                std::string name;
                const ImportType it = p.tryGetImport(name);
                if (it == ImportType::quotes)
                {
                    const fs::path referencedPath = canonicalpath.parent_path() / name;
                    realNames.createSymlinkIfNecessary(referencedPath);
                }
                else if (it == ImportType::angles)
                {
                    for (const fs::path& libpath : libpaths)
                    {
                        const fs::path referencedPath = libpath / name;
                        realNames.createSymlinkIfNecessary(referencedPath);
                    }
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        std::printf("Fel: %s\n", e.what());
        return 1;
    }
    catch (...)
    {
        std::puts("Okänt fel");
        return 1;
    }
}