// VoxelCraft.cpp
// Single-file voxel game prototype.
// Build (Linux):
// g++ VoxelCraft.cpp -std=c++17 -O2 -o VoxelCraft -lglfw -lGL -lGLU -lglut

#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/freeglut.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// ============================================================
// Constants
// ============================================================

constexpr int WINDOW_W = 1280;
constexpr int WINDOW_H = 720;

constexpr int WORLD_HEIGHT = 256;
constexpr int CHUNK_SIZE = 16;

constexpr int WORLD_MIN_X = -15000000;
constexpr int WORLD_MAX_X =  15000000;
constexpr int WORLD_MIN_Z = -15000000;
constexpr int WORLD_MAX_Z =  15000000;

constexpr float REACH = 6.0f;
constexpr float PLAYER_HEIGHT = 1.8f;
constexpr float PLAYER_WIDTH = 0.6f;

constexpr float PI = 3.14159265358979323846f;

// ============================================================
// Game state
// ============================================================

enum class GameState
{
    TITLE,
    CREATE_WORLD,
    LOADING,
    PLAYING
};

GameState gameState = GameState::TITLE;

std::string selectedWorldName = "World";
std::string currentWorldPath = "saves/World";

uint64_t worldSeed = 0;

bool firstPerson = true;
bool mouseCaptured = false;
bool inventoryOpen = false;

bool leftWasDown = false;
bool rightWasDown = false;
bool f2WasDown = false;
bool f5WasDown = false;
bool escWasDown = false;

double lastMouseX = 0;
double lastMouseY = 0;
bool firstMouse = true;

float mouseSensitivity = 0.0025f;
int scrollDelta = 0;

GLFWwindow* window = nullptr;

// ============================================================
// Utility
// ============================================================

static uint64_t Hash64(uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static uint64_t Hash3(int x, int y, int z, uint64_t seed)
{
    uint64_t h = seed;
    h ^= Hash64((uint64_t)(int64_t)x + 0x9e3779b97f4a7c15ULL);
    h ^= Hash64((uint64_t)(int64_t)y + 0x243f6a8885a308d3ULL);
    h ^= Hash64((uint64_t)(int64_t)z + 0x13198a2e03707344ULL);
    return Hash64(h);
}

static float Random01(int x, int y, int z, uint64_t seed)
{
    return float(Hash3(x, y, z, seed) & 0xFFFFFF) / float(0xFFFFFF);
}

static float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static float Fade(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

static float Noise2D(float x, float z, uint64_t seed)
{
    int x0 = (int)std::floor(x);
    int z0 = (int)std::floor(z);

    float fx = x - x0;
    float fz = z - z0;

    float a = Random01(x0, 0, z0, seed);
    float b = Random01(x0 + 1, 0, z0, seed);
    float c = Random01(x0, 0, z0 + 1, seed);
    float d = Random01(x0 + 1, 0, z0 + 1, seed);

    fx = Fade(fx);
    fz = Fade(fz);

    return Lerp(Lerp(a, b, fx), Lerp(c, d, fx), fz) * 2.0f - 1.0f;
}

static float Noise3D(float x, float y, float z, uint64_t seed)
{
    int x0 = (int)std::floor(x);
    int y0 = (int)std::floor(y);
    int z0 = (int)std::floor(z);

    float fx = Fade(x - x0);
    float fy = Fade(y - y0);
    float fz = Fade(z - z0);

    float v000 = Random01(x0,     y0,     z0,     seed);
    float v100 = Random01(x0 + 1, y0,     z0,     seed);
    float v010 = Random01(x0,     y0 + 1, z0,     seed);
    float v110 = Random01(x0 + 1, y0 + 1, z0,     seed);
    float v001 = Random01(x0,     y0,     z0 + 1, seed);
    float v101 = Random01(x0 + 1, y0,     z0 + 1, seed);
    float v011 = Random01(x0,     y0 + 1, z0 + 1, seed);
    float v111 = Random01(x0 + 1, y0 + 1, z0 + 1, seed);

    float x00 = Lerp(v000, v100, fx);
    float x10 = Lerp(v010, v110, fx);
    float x01 = Lerp(v001, v101, fx);
    float x11 = Lerp(v011, v111, fx);

    return Lerp(Lerp(x00, x10, fy), Lerp(x01, x11, fy), fz) * 2.0f - 1.0f;
}

static float Fractal2D(float x, float z, uint64_t seed)
{
    float result = 0;
    float amplitude = 1;
    float frequency = 1;
    float total = 0;

    for (int i = 0; i < 5; ++i)
    {
        result += Noise2D(x * frequency, z * frequency, seed + i * 991) * amplitude;
        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return result / total;
}

static float Fractal3D(float x, float y, float z, uint64_t seed)
{
    float result = 0;
    float amplitude = 1;
    float frequency = 1;
    float total = 0;

    for (int i = 0; i < 4; ++i)
    {
        result += Noise3D(x * frequency, y * frequency, z * frequency,
                          seed + i * 577) * amplitude;
        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return result / total;
}

// ============================================================
// Blocks
// ============================================================

enum BlockID : uint16_t
{
    AIR = 0,
    STONE,
    GRASS,
    DIRT,
    SAND,
    SNOW,
    IRON_ORE,
    GOLD_ORE,
    REDSTONE_ORE,
    DIAMOND_ORE,
    EMERALD_ORE,
    WATER,
    WOOD,
    LEAVES,
    COBBLESTONE,
    PLANKS,
    GLASS,
    GRAVEL,
    COAL_ORE,
    BEDROCK,
    CACTUS,
    AUTUMN_LEAVES,
    TORCH
};

static bool IsSolidBlock(uint16_t id)
{
    return id != AIR && id != WATER && id != TORCH;
}

static bool IsTransparentBlock(uint16_t id)
{
    return id == AIR || id == WATER || id == GLASS || id == LEAVES ||
           id == AUTUMN_LEAVES || id == TORCH;
}

// ============================================================
// Items
// ============================================================

enum class ItemClass
{
    NONE,
    BLOCK,
    WEAPON,
    TOOL,
    FOOD,
    ARMOR,
    MATERIAL
};

enum class ArmorSlot
{
    HELMET = 0,
    CHESTPLATE,
    LEGGINGS,
    BOOTS
};

enum Enchantment
{
    SHARPNESS,
    FIRE_ASPECT,
    UNBREAKING,
    RESTORE,
    AUTUMN_REPAIR,
    PROTECTION,
    FEATHERFALL,
    HASTE,
    FORTUNE,
    EFFICIENCY,
    WATERBREATHING,
    FROSTSTEP,
    LIFEBLOOM,
    HEATSHIELD,
    ADRENALINE_CONTROL,
    LUCK_OF_SEA,
    INEFFICIENCY
};

struct ItemDefinition
{
    int id = 0;
    std::string name;
    std::string shortName;
    ItemClass type = ItemClass::NONE;
    uint16_t block = AIR;
    int maxStack = 64;
    int damage = 0;
    int durability = 0;
    ArmorSlot armorSlot = ArmorSlot::HELMET;
    int armorProtection = 0;
    bool hasArmorSlot = false;
    bool edible = false;
    int hunger = 0;
};

static std::vector<ItemDefinition> itemDefinitions;

static void AddItem(
    int id,
    const std::string& name,
    const std::string& shortName,
    ItemClass type,
    int maxStack = 64,
    int damage = 0,
    int durability = 0,
    uint16_t block = AIR)
{
    ItemDefinition d;
    d.id = id;
    d.name = name;
    d.shortName = shortName;
    d.type = type;
    d.maxStack = maxStack;
    d.damage = damage;
    d.durability = durability;
    d.block = block;
    itemDefinitions.push_back(d);
}

static void AddArmor(
    int id,
    const std::string& name,
    const std::string& shortName,
    ArmorSlot slot,
    int protection,
    int durability)
{
    ItemDefinition d;
    d.id = id;
    d.name = name;
    d.shortName = shortName;
    d.type = ItemClass::ARMOR;
    d.maxStack = 1;
    d.durability = durability;
    d.armorSlot = slot;
    d.armorProtection = protection;
    d.hasArmorSlot = true;
    itemDefinitions.push_back(d);
}

static void AddFood(
    int id,
    const std::string& name,
    const std::string& shortName,
    int hunger)
{
    ItemDefinition d;
    d.id = id;
    d.name = name;
    d.shortName = shortName;
    d.type = ItemClass::FOOD;
    d.maxStack = 64;
    d.edible = true;
    d.hunger = hunger;
    itemDefinitions.push_back(d);
}

static const ItemDefinition* GetItem(int id)
{
    for (const auto& item : itemDefinitions)
        if (item.id == id)
            return &item;
    return nullptr;
}

static void InitItems()
{
    itemDefinitions.clear();

    // Blocks
    AddItem(1,  "Stone",          "st", ItemClass::BLOCK, 64, 0, 0, STONE);
    AddItem(2,  "Grass Block",    "gr", ItemClass::BLOCK, 64, 0, 0, GRASS);
    AddItem(3,  "Dirt",           "di", ItemClass::BLOCK, 64, 0, 0, DIRT);
    AddItem(4,  "Sand",           "sa", ItemClass::BLOCK, 64, 0, 0, SAND);
    AddItem(5,  "Snow",           "sn", ItemClass::BLOCK, 64, 0, 0, SNOW);
    AddItem(6,  "Cobblestone",    "co", ItemClass::BLOCK, 64, 0, 0, COBBLESTONE);
    AddItem(7,  "Planks",          "pl", ItemClass::BLOCK, 64, 0, 0, PLANKS);
    AddItem(8,  "Glass",           "gl", ItemClass::BLOCK, 64, 0, 0, GLASS);
    AddItem(9,  "Gravel",          "gv", ItemClass::BLOCK, 64, 0, 0, GRAVEL);
    AddItem(10, "Wood",            "wo", ItemClass::BLOCK, 64, 0, 0, WOOD);
    AddItem(11, "Leaves",          "lv", ItemClass::BLOCK, 64, 0, 0, LEAVES);
    AddItem(12, "Autumn Leaves",   "al", ItemClass::BLOCK, 64, 0, 0, AUTUMN_LEAVES);
    AddItem(13, "Cactus",          "ca", ItemClass::BLOCK, 64, 0, 0, CACTUS);
    AddItem(14, "Torch",           "to", ItemClass::BLOCK, 64, 0, 0, TORCH);

    // Materials
    AddItem(50, "Stick",            "sk", ItemClass::MATERIAL);
    AddItem(51, "Coal",             "cl", ItemClass::MATERIAL);
    AddItem(52, "Iron Ingot",       "ii", ItemClass::MATERIAL);
    AddItem(53, "Gold Ingot",       "gi", ItemClass::MATERIAL);
    AddItem(54, "Diamond",          "dm", ItemClass::MATERIAL);
    AddItem(55, "Emerald",          "em", ItemClass::MATERIAL);
    AddItem(56, "Leather",          "le", ItemClass::MATERIAL);
    AddItem(57, "Flint",            "fl", ItemClass::MATERIAL);
    AddItem(58, "Autumn Leaf",      "af", ItemClass::MATERIAL);
    AddItem(59, "String",           "ss", ItemClass::MATERIAL);

    // Weapons
    AddItem(100, "Wooden Sword",     "ws", ItemClass::WEAPON, 1, 4, 59);
    AddItem(101, "Stone Sword",      "ss", ItemClass::WEAPON, 1, 5, 131);
    AddItem(102, "Iron Sword",       "is", ItemClass::WEAPON, 1, 6, 250);
    AddItem(103, "Diamond Sword",    "ds", ItemClass::WEAPON, 1, 8, 1561);

    // Pickaxes
    AddItem(110, "Wooden Pickaxe",   "wp", ItemClass::TOOL, 1, 0, 59);
    AddItem(111, "Stone Pickaxe",    "sp", ItemClass::TOOL, 1, 0, 131);
    AddItem(112, "Iron Pickaxe",     "ip", ItemClass::TOOL, 1, 0, 250);
    AddItem(113, "Diamond Pickaxe",  "dp", ItemClass::TOOL, 1, 0, 1561);

    // Axes
    AddItem(120, "Wooden Axe",       "wa", ItemClass::TOOL, 1, 0, 59);
    AddItem(121, "Stone Axe",        "xa", ItemClass::TOOL, 1, 0, 131);
    AddItem(122, "Iron Axe",         "ia", ItemClass::TOOL, 1, 0, 250);
    AddItem(123, "Diamond Axe",      "da", ItemClass::TOOL, 1, 0, 1561);

    // Shovels
    AddItem(130, "Wooden Shovel",    "wsh", ItemClass::TOOL, 1, 0, 59);
    AddItem(131, "Stone Shovel",     "ssh", ItemClass::TOOL, 1, 0, 131);
    AddItem(132, "Iron Shovel",      "ish", ItemClass::TOOL, 1, 0, 250);
    AddItem(133, "Diamond Shovel",   "dsh", ItemClass::TOOL, 1, 0, 1561);

    // Food
    AddFood(200, "Apple",  "ap", 4);
    AddFood(201, "Bread",  "br", 5);
    AddFood(202, "Mushroom", "mu", 3);
    AddFood(203, "Cooked Beef", "cb", 8);

    // Armor
    AddArmor(300, "Diamond Helmet",     "dh", ArmorSlot::HELMET,     3, 363);
    AddArmor(301, "Diamond Chestplate", "dc", ArmorSlot::CHESTPLATE, 8, 528);
    AddArmor(302, "Diamond Leggings",   "dl", ArmorSlot::LEGGINGS,   6, 495);
    AddArmor(303, "Diamond Boots",      "db", ArmorSlot::BOOTS,      3, 429);

    AddArmor(310, "Iron Helmet",     "ih", ArmorSlot::HELMET,     2, 165);
    AddArmor(311, "Iron Chestplate", "ic", ArmorSlot::CHESTPLATE, 6, 240);
    AddArmor(312, "Iron Leggings",   "il", ArmorSlot::LEGGINGS,   5, 225);
    AddArmor(313, "Iron Boots",      "ib", ArmorSlot::BOOTS,      2, 195);

    // Give the player useful starting items.
}

// ============================================================
// Inventory
// ============================================================

struct InventorySlot
{
    int itemID = 0;
    int count = 0;
    int durability = 0;
};

class Inventory
{
public:
    static constexpr int SIZE = 36;
    static constexpr int HOTBAR = 9;

    InventorySlot slots[SIZE];
    int selected = 0;

    InventorySlot* GetSelected()
    {
        return &slots[selected];
    }

    const InventorySlot* GetSelected() const
    {
        return &slots[selected];
    }

    const ItemDefinition* GetSelectedItem() const
    {
        return GetItem(slots[selected].itemID);
    }

    uint16_t GetSelectedBlock() const
    {
        const ItemDefinition* item = GetSelectedItem();
        return item ? item->block : AIR;
    }

    bool AddItem(int itemID, int amount = 1)
    {
        const ItemDefinition* def = GetItem(itemID);
        if (!def || amount <= 0)
            return false;

        // Existing stacks.
        for (int i = 0; i < SIZE && amount > 0; ++i)
        {
            if (slots[i].itemID == itemID && slots[i].count < def->maxStack)
            {
                int add = std::min(amount, def->maxStack - slots[i].count);
                slots[i].count += add;
                amount -= add;
            }
        }

        // Empty slots.
        for (int i = 0; i < SIZE && amount > 0; ++i)
        {
            if (slots[i].itemID == 0)
            {
                int add = std::min(amount, def->maxStack);
                slots[i].itemID = itemID;
                slots[i].count = add;
                slots[i].durability = def->durability;
                amount -= add;
            }
        }

        return amount == 0;
    }

    bool ConsumeSelected(int amount = 1)
    {
        if (slots[selected].itemID == 0 || slots[selected].count < amount)
            return false;

        slots[selected].count -= amount;

        if (slots[selected].count <= 0)
        {
            slots[selected] = {};
        }

        return true;
    }

    void Select(int slot)
    {
        if (slot >= 0 && slot < HOTBAR)
            selected = slot;
    }
};

// ============================================================
// Armor
// ============================================================

class ArmorHandler
{
public:
    InventorySlot armor[4];
    float repairTimer = 0.0f;

    int GetProtection() const
    {
        int total = 0;

        for (int i = 0; i < 4; ++i)
        {
            const ItemDefinition* d = GetItem(armor[i].itemID);
            if (d)
                total += d->armorProtection;
        }

        return total;
    }

    bool Equip(Inventory& inv, int inventorySlot)
    {
        if (inventorySlot < 0 || inventorySlot >= Inventory::SIZE)
            return false;

        InventorySlot& source = inv.slots[inventorySlot];
        const ItemDefinition* d = GetItem(source.itemID);

        if (!d || !d->hasArmorSlot)
            return false;

        int slot = (int)d->armorSlot;

        if (armor[slot].itemID != 0)
            return false;

        armor[slot] = source;
        source = {};
        return true;
    }

    bool Unequip(Inventory& inv, int armorSlot)
    {
        if (armorSlot < 0 || armorSlot >= 4)
            return false;

        if (armor[armorSlot].itemID == 0)
            return false;

        if (!inv.AddItem(armor[armorSlot].itemID, armor[armorSlot].count))
            return false;

        armor[armorSlot] = {};
        return true;
    }

    void Update(float dt)
    {
        repairTimer += dt;

        if (repairTimer >= 20.0f)
        {
            repairTimer = 0.0f;

            for (auto& slot : armor)
            {
                if (slot.itemID != 0 && slot.durability > 0)
                    slot.durability++;
            }
        }
    }
};

// ============================================================
// Player stats
// ============================================================

struct PlayerStats
{
    float health = 20.0f;
    float maxHealth = 20.0f;
    float hunger = 20.0f;
    float adrenaline = 0.0f;
    float heat = 37.0f;
    float highAdrenalineTime = 0.0f;
    bool gameOver = false;

    float StrengthMultiplier() const
    {
        return 1.0f + (adrenaline / 100.0f) * 3.0f;
    }

    void Update(float dt)
    {
        if (gameOver)
            return;

        adrenaline = std::max(0.0f, adrenaline - dt * 1.5f);

        if (adrenaline >= 80.0f)
            highAdrenalineTime += dt;
        else
            highAdrenalineTime = std::max(0.0f, highAdrenalineTime - dt * 2.0f);

        if (highAdrenalineTime >= 300.0f)
            gameOver = true;

        hunger = std::max(0.0f, hunger - dt * 0.002f);
    }

    void Damage(float amount)
    {
        health -= amount;

        if (health <= 0.0f)
        {
            health = 0.0f;
            gameOver = true;
        }

        adrenaline = std::min(100.0f, adrenaline + amount * 5.0f);
    }
};

// ============================================================
// Entities
// ============================================================

enum class EntityType
{
    PLAYER,
    MOB
};

class Entity
{
public:
    EntityType type = EntityType::MOB;
    float x = 0;
    float y = 0;
    float z = 0;
    float health = 10;
    bool alive = true;

    virtual ~Entity() = default;

    virtual void Damage(float amount)
    {
        if (!alive)
            return;

        health -= amount;

        if (health <= 0)
        {
            health = 0;
            alive = false;
        }
    }

    virtual void Interact()
    {
        // Default entity interaction.
    }
};

class Mob : public Entity
{
public:
    Mob(float px, float py, float pz)
    {
        type = EntityType::MOB;
        x = px;
        y = py;
        z = pz;
        health = 20;
    }

    void Interact() override
    {
        // Simple interaction: give a tiny adrenaline boost.
        std::cout << "You interacted with a mob.\n";
    }
};

static std::vector<Mob> mobs;

// ============================================================
// Terrain
// ============================================================

enum Biome
{
    FOREST,
    PLAINS,
    SNOW_BIOME,
    TROPICAL,
    AUTUMN,
    FLOODLANDS,
    MOUNTAIN,
    COLD_DESERT,
    DARK_FOREST
};

static Biome GetBiome(int x, int z)
{
    float n = Fractal2D(x * 0.003f, z * 0.003f, worldSeed + 7000);

    if (n < -0.55f) return SNOW_BIOME;
    if (n < -0.30f) return COLD_DESERT;
    if (n < -0.10f) return PLAINS;
    if (n <  0.12f) return FOREST;
    if (n <  0.30f) return AUTUMN;
    if (n <  0.48f) return DARK_FOREST;
    if (n <  0.65f) return TROPICAL;
    if (n <  0.80f) return FLOODLANDS;
    return MOUNTAIN;
}

static int GetTerrainHeight(int x, int z)
{
    float broad = Fractal2D(x * 0.004f, z * 0.004f, worldSeed + 100);
    float detail = Fractal2D(x * 0.025f, z * 0.025f, worldSeed + 200);

    float h = 66.0f + broad * 35.0f + detail * 8.0f;

    Biome b = GetBiome(x, z);

    if (b == MOUNTAIN)
        h += std::abs(broad) * 35.0f;

    if (b == FLOODLANDS)
        h -= 8.0f;

    return std::clamp((int)h, 4, WORLD_HEIGHT - 5);
}

static bool IsCave(int x, int y, int z)
{
    if (y < 5 || y > 100)
        return false;

    float n = Fractal3D(x * 0.035f, y * 0.035f, z * 0.035f, worldSeed + 5000);

    float threshold = 0.62f;

    // Caves become smaller deeper down.
    if (y < 25)
        threshold = 0.69f;

    return n > threshold;
}

static uint16_t GenerateBlock(int x, int y, int z)
{
    if (y < 0 || y >= WORLD_HEIGHT)
        return AIR;

    int surface = GetTerrainHeight(x, z);

    if (y > surface)
    {
        Biome b = GetBiome(x, z);

        if (b == FLOODLANDS && y <= 62)
            return WATER;

        return AIR;
    }

    if (y == 0)
        return BEDROCK;

    if (IsCave(x, y, z) && y < surface - 3)
        return AIR;

    // Ores.
    float ore = Random01(x * 3 + y, y * 7, z * 11, worldSeed + 9000);

    if (y <= 12 && ore > 0.985f)
        return DIAMOND_ORE;

    if (y <= 20 && ore > 0.965f)
        return REDSTONE_ORE;

    if (y <= 32 && ore > 0.955f)
        return GOLD_ORE;

    if (y <= 64 && ore > 0.935f)
        return IRON_ORE;

    if (y <= 80 && ore > 0.975f)
        return EMERALD_ORE;

    if (y <= 90 && ore > 0.90f)
        return COAL_ORE;

    Biome b = GetBiome(x, z);

    if (y == surface)
    {
        if (b == SNOW_BIOME || b == MOUNTAIN)
            return SNOW;

        if (b == COLD_DESERT || b == TROPICAL || b == FLOODLANDS)
            return SAND;

        return GRASS;
    }

    if (y >= surface - 3)
    {
        if (b == COLD_DESERT || b == TROPICAL || b == FLOODLANDS)
            return SAND;

        return DIRT;
    }

    return STONE;
}

// ============================================================
// Chunk / World
// ============================================================

static uint64_t PackModificationKey(int x, int y, int z)
{
    uint64_t ux = (uint32_t)x;
    uint64_t uy = (uint32_t)y;
    uint64_t uz = (uint32_t)z;

    return (ux << 32) ^ ((uy & 0xFFFF) << 16) ^ (uz & 0xFFFF);
}

static void UnpackModificationKey(uint64_t key, int& x, int& y, int& z)
{
    x = (int)(uint32_t)(key >> 32);
    y = (int)((key >> 16) & 0xFFFF);
    z = (int)(uint16_t)(key & 0xFFFF);
}

class Chunk
{
public:
    int cx = 0;
    int cz = 0;

    std::unordered_map<uint64_t, uint16_t> modifications;
    bool dirty = false;

    Chunk() = default;

    Chunk(int x, int z)
        : cx(x), cz(z)
    {
    }
};

static int FloorDiv(int a, int b)
{
    int q = a / b;
    int r = a % b;

    if (r != 0 && ((r < 0) != (b < 0)))
        --q;

    return q;
}

class World
{
public:
    std::unordered_map<long long, Chunk> chunks;
    bool dirty = false;

    explicit World(const std::string& path)
    {
        currentWorldPath = path;
        fs::create_directories(path);
        LoadMetadata();
    }

    ~World()
    {
        SaveAll();
    }

    long long ChunkKey(int cx, int cz) const
    {
        return (long long)cx << 32 ^ (uint32_t)cz;
    }

    Chunk& GetChunk(int cx, int cz)
    {
        long long key = ChunkKey(cx, cz);

        auto it = chunks.find(key);

        if (it == chunks.end())
        {
            Chunk c(cx, cz);
            LoadChunk(c);
            auto result = chunks.emplace(key, std::move(c));
            return result.first->second;
        }

        return it->second;
    }

    uint16_t GetBlock(int x, int y, int z)
    {
        if (y < 0 || y >= WORLD_HEIGHT)
            return AIR;

        int cx = FloorDiv(x, CHUNK_SIZE);
        int cz = FloorDiv(z, CHUNK_SIZE);

        Chunk& c = GetChunk(cx, cz);

        uint64_t key = PackModificationKey(x, y, z);

        auto it = c.modifications.find(key);

        if (it != c.modifications.end())
            return it->second;

        return GenerateBlock(x, y, z);
    }

    void SetBlock(int x, int y, int z, uint16_t id)
    {
        if (y < 0 || y >= WORLD_HEIGHT)
            return;

        int cx = FloorDiv(x, CHUNK_SIZE);
        int cz = FloorDiv(z, CHUNK_SIZE);

        Chunk& c = GetChunk(cx, cz);

        uint64_t key = PackModificationKey(x, y, z);

        uint16_t generated = GenerateBlock(x, y, z);

        if (id == generated)
            c.modifications.erase(key);
        else
            c.modifications[key] = id;

        c.dirty = true;
        dirty = true;
    }

    void BreakBlock(int x, int y, int z)
    {
        uint16_t id = GetBlock(x, y, z);

        if (id == AIR || id == BEDROCK)
            return;

        SetBlock(x, y, z, AIR);
    }

    void SaveChunk(Chunk& c)
    {
        if (!c.dirty)
            return;

        std::string filename =
            currentWorldPath + "/chunk_" +
            std::to_string(c.cx) + "_" +
            std::to_string(c.cz) + ".dat";

        std::ofstream out(filename, std::ios::binary);

        if (!out)
            return;

        uint32_t count = (uint32_t)c.modifications.size();
        out.write((char*)&count, sizeof(count));

        for (const auto& pair : c.modifications)
        {
            out.write((char*)&pair.first, sizeof(pair.first));
            out.write((char*)&pair.second, sizeof(pair.second));
        }

        c.dirty = false;
    }

    void SaveAll()
    {
        for (auto& pair : chunks)
            SaveChunk(pair.second);

        SaveMetadata();
        dirty = false;
    }

private:
    void LoadChunk(Chunk& c)
    {
        std::string filename =
            currentWorldPath + "/chunk_" +
            std::to_string(c.cx) + "_" +
            std::to_string(c.cz) + ".dat";

        std::ifstream in(filename, std::ios::binary);

        if (!in)
            return;

        uint32_t count = 0;
        in.read((char*)&count, sizeof(count));

        for (uint32_t i = 0; i < count; ++i)
        {
            uint64_t key;
            uint16_t block;

            in.read((char*)&key, sizeof(key));
            in.read((char*)&block, sizeof(block));

            if (in)
                c.modifications[key] = block;
        }
    }

    void SaveMetadata()
    {
        std::ofstream out(currentWorldPath + "/world.dat");

        if (!out)
            return;

        out << "VOXELCRAFT_WORLD\n";
        out << "seed " << worldSeed << "\n";
        out << "version 1\n";
    }

    void LoadMetadata()
    {
        std::ifstream in(currentWorldPath + "/world.dat");

        if (!in)
            return;

        std::string tag;

        while (in >> tag)
        {
            if (tag == "seed")
                in >> worldSeed;
        }
    }
};

static World* world = nullptr;

// ============================================================
// Player
// ============================================================

struct Player
{
    float x = 0.5f;
    float y = 80.0f;
    float z = 0.5f;

    float yaw = 0.0f;
    float pitch = 0.0f;

    float velocityY = 0.0f;
    bool onGround = false;

    Inventory inventory;
    ArmorHandler armor;
    PlayerStats stats;

    void Reset()
    {
        x = 0.5f;
        z = 0.5f;

        int surface = GetTerrainHeight(0, 0);
        y = surface + 2.0f;

        velocityY = 0;
        onGround = false;

        yaw = 0;
        pitch = 0;
    }
};

static Player player;

// ============================================================
// Raycasting
// ============================================================

struct RaycastHit
{
    bool hit = false;
    bool entityHit = false;

    Entity* entity = nullptr;

    int x = 0;
    int y = 0;
    int z = 0;

    int nx = 0;
    int ny = 0;
    int nz = 0;

    float distance = 0;
};

static void GetLookVector(float& dx, float& dy, float& dz)
{
    float cp = std::cos(player.pitch);
    float sp = std::sin(player.pitch);
    float cy = std::cos(player.yaw);
    float sy = std::sin(player.yaw);

    dx = sy * cp;
    dy = -sp;
    dz = -cy * cp;
}

static RaycastHit RaycastWorld()
{
    RaycastHit result;

    float dx, dy, dz;
    GetLookVector(dx, dy, dz);

    // Entities first.
    for (auto& mob : mobs)
    {
        if (!mob.alive)
            continue;

        float ex = mob.x - player.x;
        float ey = (mob.y + 0.9f) - (player.y + 1.5f);
        float ez = mob.z - player.z;

        float along = ex * dx + ey * dy + ez * dz;

        if (along < 0 || along > REACH)
            continue;

        float px = ex - dx * along;
        float py = ey - dy * along;
        float pz = ez - dz * along;

        float distanceSq = px * px + py * py + pz * pz;

        if (distanceSq < 0.7f * 0.7f)
        {
            result.hit = true;
            result.entityHit = true;
            result.entity = &mob;
            result.distance = along;
            return result;
        }
    }

    float ox = player.x;
    float oy = player.y + 1.5f;
    float oz = player.z;

    int lastX = (int)std::floor(ox);
    int lastY = (int)std::floor(oy);
    int lastZ = (int)std::floor(oz);

    for (float distance = 0; distance <= REACH; distance += 0.05f)
    {
        float px = ox + dx * distance;
        float py = oy + dy * distance;
        float pz = oz + dz * distance;

        int bx = (int)std::floor(px);
        int by = (int)std::floor(py);
        int bz = (int)std::floor(pz);

        if (bx == lastX && by == lastY && bz == lastZ)
            continue;

        uint16_t block = world->GetBlock(bx, by, bz);

        if (block != AIR && block != WATER && block != TORCH)
        {
            result.hit = true;
            result.x = bx;
            result.y = by;
            result.z = bz;
            result.distance = distance;

            result.nx = lastX - bx;
            result.ny = lastY - by;
            result.nz = lastZ - bz;

            return result;
        }

        lastX = bx;
        lastY = by;
        lastZ = bz;
    }

    return result;
}

// ============================================================
// Player movement
// ============================================================

static bool IsPlayerInsideSolid(float px, float py, float pz)
{
    int minX = (int)std::floor(px - PLAYER_WIDTH * 0.5f);
    int maxX = (int)std::floor(px + PLAYER_WIDTH * 0.5f);
    int minY = (int)std::floor(py);
    int maxY = (int)std::floor(py + PLAYER_HEIGHT);
    int minZ = (int)std::floor(pz - PLAYER_WIDTH * 0.5f);
    int maxZ = (int)std::floor(pz + PLAYER_WIDTH * 0.5f);

    for (int x = minX; x <= maxX; ++x)
        for (int y = minY; y <= maxY; ++y)
            for (int z = minZ; z <= maxZ; ++z)
                if (IsSolidBlock(world->GetBlock(x, y, z)))
                    return true;

    return false;
}

static void MovePlayer(float dx, float dy, float dz)
{
    float nx = player.x + dx;

    if (!IsPlayerInsideSolid(nx, player.y, player.z))
        player.x = nx;

    float nz = player.z + dz;

    if (!IsPlayerInsideSolid(player.x, player.y, nz))
        player.z = nz;

    float ny = player.y + dy;

    if (!IsPlayerInsideSolid(player.x, ny, player.z))
    {
        player.y = ny;
        player.onGround = false;
    }
    else
    {
        if (dy < 0)
            player.onGround = true;

        player.velocityY = 0;
    }
}

static void UpdatePlayer(float dt)
{
    if (inventoryOpen || player.stats.gameOver)
        return;

    float forward = 0;
    float strafe = 0;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) forward += 1;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) forward -= 1;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) strafe += 1;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) strafe -= 1;

    float length = std::sqrt(forward * forward + strafe * strafe);

    if (length > 0)
    {
        forward /= length;
        strafe /= length;
    }

    float speed = 4.5f;

    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        speed = 7.5f;

    float forwardX = std::sin(player.yaw);
    float forwardZ = -std::cos(player.yaw);

    float rightX = std::cos(player.yaw);
    float rightZ = std::sin(player.yaw);

    float dx = (forwardX * forward + rightX * strafe) * speed * dt;
    float dz = (forwardZ * forward + rightZ * strafe) * speed * dt;

    MovePlayer(dx, 0, dz);

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && player.onGround)
    {
        player.velocityY = 7.5f;
        player.onGround = false;
    }

    player.velocityY -= 20.0f * dt;
    player.velocityY = std::max(player.velocityY, -30.0f);

    MovePlayer(0, player.velocityY * dt, 0);

    player.stats.Update(dt);
    player.armor.Update(dt);
}

// ============================================================
// Rendering helpers
// ============================================================

static void DrawText(float x, float y, const std::string& text, float scale = 1.0f)
{
    glPushMatrix();
    glTranslatef(x, y, 0);
    glScalef(scale, scale, scale);

    glColor3f(1, 1, 1);

    for (char c : text)
        glutBitmapCharacter(GLUT_BITMAP_8_BY_13, c);

    glPopMatrix();
}

static void DrawCenteredText(float y, const std::string& text, float scale = 1.0f)
{
    float width = text.size() * 8.0f * scale;
    DrawText((WINDOW_W - width) * 0.5f, y, text, scale);
}

static void DrawRect(float x, float y, float w, float h,
                     float r, float g, float b, float a = 1.0f)
{
    glColor4f(r, g, b, a);

    glBegin(GL_QUADS);
    glVertex2f(x,     y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x,     y + h);
    glEnd();
}

static void DrawOutline(float x, float y, float w, float h,
                        float r, float g, float b)
{
    glColor3f(r, g, b);

    glBegin(GL_LINE_LOOP);
    glVertex2f(x,     y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x,     y + h);
    glEnd();
}

static bool PointInRect(double mx, double my,
                        float x, float y, float w, float h)
{
    return mx >= x && mx <= x + w &&
           my >= y && my <= y + h;
}

static void Setup2D()
{
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

static void End2D()
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

// ============================================================
// Title / Create World GUI
// ============================================================

static bool Button(float x, float y, float w, float h,
                   const std::string& label,
                   double mouseX, double mouseY,
                   bool clicked)
{
    bool hovered = PointInRect(mouseX, mouseY, x, y, w, h);

    DrawRect(x, y, w, h,
             hovered ? 0.20f : 0.12f,
             hovered ? 0.35f : 0.20f,
             hovered ? 0.55f : 0.30f);

    DrawOutline(x, y, w, h, 0.65f, 0.75f, 0.90f);

    float textWidth = label.size() * 8.0f;
    DrawText(x + (w - textWidth) * 0.5f,
             y + h * 0.5f + 5,
             label);

    return hovered && clicked;
}

static void RenderTitleScreen()
{
    Setup2D();

    DrawRect(0, 0, WINDOW_W, WINDOW_H, 0.025f, 0.035f, 0.06f);

    DrawCenteredText(120, "VOXELCRAFT", 3.0f);
    DrawCenteredText(165, "A single-file voxel adventure", 1.0f);

    double mx, my;
    glfwGetCursorPos(window, &mx, &my);

    bool click = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    if (Button(440, 260, 400, 60, "CREATE WORLD", mx, my, click))
    {
        gameState = GameState::CREATE_WORLD;
        selectedWorldName.clear();
        mouseCaptured = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    if (Button(440, 340, 400, 60, "LOAD WORLD", mx, my, click))
    {
        // Load the default/last world.
        selectedWorldName = "World";
        currentWorldPath = "saves/World";

        if (!fs::exists(currentWorldPath))
            fs::create_directories(currentWorldPath);

        std::ifstream meta(currentWorldPath + "/world.dat");

        if (!meta)
        {
            std::random_device rd;
            worldSeed = ((uint64_t)rd() << 32) ^ rd();

            std::ofstream out(currentWorldPath + "/world.dat");
            out << "VOXELCRAFT_WORLD\n";
            out << "seed " << worldSeed << "\n";
            out << "version 1\n";
        }

        delete world;
        world = new World(currentWorldPath);
        player.Reset();

        mobs.clear();
        mobs.emplace_back(5.5f, GetTerrainHeight(5, 5) + 1.0f, 5.5f);

        gameState = GameState::PLAYING;
        mouseCaptured = true;
        firstMouse = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }

    if (Button(440, 420, 400, 60, "QUIT", mx, my, click))
    {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    DrawCenteredText(650, "WASD Move  |  Mouse Look  |  F2 Inventory", 1.0f);

    End2D();
}

static std::string SanitizeWorldName(std::string name)
{
    std::string result;

    for (char c : name)
    {
        if (std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ')
            result += c;
    }

    while (!result.empty() && result.back() == ' ')
        result.pop_back();

    if (result.empty())
        result = "World";

    return result;
}

static void RenderCreateWorldScreen()
{
    Setup2D();

    DrawRect(0, 0, WINDOW_W, WINDOW_H, 0.025f, 0.035f, 0.06f);

    DrawCenteredText(100, "CREATE WORLD", 2.5f);

    DrawText(300, 220, "WORLD NAME");

    DrawRect(300, 245, 680, 55, 0.08f, 0.10f, 0.15f);
    DrawOutline(300, 245, 680, 55, 0.6f, 0.7f, 0.85f);

    std::string shown = selectedWorldName;

    if (shown.empty())
        shown = "World";

    DrawText(320, 280, shown);

    DrawText(300, 335, "Type a name, then press CREATE.");
    DrawText(300, 365, "Worlds are stored in the saves/ directory.");
    DrawText(300, 395, "The seed is generated automatically and saved privately.");

    double mx, my;
    glfwGetCursorPos(window, &mx, &my);

    bool click = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    if (Button(300, 470, 300, 60, "CREATE", mx, my, click))
    {
        std::string clean = SanitizeWorldName(selectedWorldName);

        currentWorldPath = "saves/" + clean;

        // Avoid accidentally deleting or replacing an existing world.
        fs::create_directories(currentWorldPath);

        std::string metadata = currentWorldPath + "/world.dat";

        if (!fs::exists(metadata))
        {
            std::random_device rd;
            worldSeed = ((uint64_t)rd() << 32) ^ rd();

            std::ofstream out(metadata);
            out << "VOXELCRAFT_WORLD\n";
            out << "seed " << worldSeed << "\n";
            out << "version 1\n";
        }

        selectedWorldName = clean;

        delete world;
        world = new World(currentWorldPath);

        player.Reset();

        mobs.clear();
        mobs.emplace_back(5.5f, GetTerrainHeight(5, 5) + 1.0f, 5.5f);

        gameState = GameState::PLAYING;
        mouseCaptured = true;
        firstMouse = true;

        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        // Starter items.
        player.inventory.AddItem(1, 32);     // Stone
        player.inventory.AddItem(3, 16);     // Dirt
        player.inventory.AddItem(7, 16);     // Planks
        player.inventory.AddItem(100, 1);    // Wooden sword
        player.inventory.AddItem(110, 1);    // Wooden pickaxe
        player.inventory.AddItem(200, 5);    // Apples
        player.inventory.AddItem(50, 16);    // Sticks
        player.inventory.AddItem(14, 16);    // Torches
    }

    if (Button(680, 470, 300, 60, "BACK", mx, my, click))
    {
        gameState = GameState::TITLE;
        selectedWorldName = "World";
    }

    End2D();
}

// ============================================================
// World rendering
// ============================================================

static void BlockColor(uint16_t id)
{
    switch (id)
    {
        case STONE:         glColor3f(0.48f, 0.48f, 0.50f); break;
        case GRASS:         glColor3f(0.25f, 0.65f, 0.25f); break;
        case DIRT:          glColor3f(0.45f, 0.27f, 0.12f); break;
        case SAND:          glColor3f(0.82f, 0.75f, 0.50f); break;
        case SNOW:          glColor3f(0.90f, 0.92f, 0.95f); break;
        case IRON_ORE:      glColor3f(0.60f, 0.50f, 0.45f); break;
        case GOLD_ORE:      glColor3f(0.90f, 0.72f, 0.12f); break;
        case REDSTONE_ORE:  glColor3f(0.65f, 0.05f, 0.04f); break;
        case DIAMOND_ORE:   glColor3f(0.20f, 0.85f, 0.90f); break;
        case EMERALD_ORE:   glColor3f(0.10f, 0.80f, 0.30f); break;
        case COAL_ORE:      glColor3f(0.10f, 0.10f, 0.10f); break;
        case WATER:         glColor4f(0.10f, 0.35f, 0.85f, 0.55f); break;
        case WOOD:          glColor3f(0.42f, 0.25f, 0.10f); break;
        case LEAVES:        glColor3f(0.12f, 0.55f, 0.18f); break;
        case AUTUMN_LEAVES: glColor3f(0.85f, 0.30f, 0.08f); break;
        case COBBLESTONE:   glColor3f(0.35f, 0.35f, 0.37f); break;
        case PLANKS:        glColor3f(0.65f, 0.46f, 0.25f); break;
        case GLASS:         glColor4f(0.60f, 0.80f, 0.95f, 0.35f); break;
        case GRAVEL:        glColor3f(0.48f, 0.45f, 0.42f); break;
        case BEDROCK:       glColor3f(0.08f, 0.08f, 0.08f); break;
        case CACTUS:        glColor3f(0.08f, 0.50f, 0.16f); break;
        case TORCH:         glColor3f(1.0f, 0.65f, 0.10f); break;
        default:            glColor3f(1, 1, 1); break;
    }
}

static void DrawCube(float x, float y, float z, uint16_t id)
{
    BlockColor(id);

    float x0 = x;
    float x1 = x + 1;
    float y0 = y;
    float y1 = y + 1;
    float z0 = z;
    float z1 = z + 1;

    glBegin(GL_QUADS);

    // Front
    glVertex3f(x0, y0, z0);
    glVertex3f(x1, y0, z0);
    glVertex3f(x1, y1, z0);
    glVertex3f(x0, y1, z0);

    // Back
    glVertex3f(x1, y0, z1);
    glVertex3f(x0, y0, z1);
    glVertex3f(x0, y1, z1);
    glVertex3f(x1, y1, z1);

    // Left
    glVertex3f(x0, y0, z1);
    glVertex3f(x0, y0, z0);
    glVertex3f(x0, y1, z0);
    glVertex3f(x0, y1, z1);

    // Right
    glVertex3f(x1, y0, z0);
    glVertex3f(x1, y0, z1);
    glVertex3f(x1, y1, z1);
    glVertex3f(x1, y1, z0);

    // Top
    glVertex3f(x0, y1, z0);
    glVertex3f(x1, y1, z0);
    glVertex3f(x1, y1, z1);
    glVertex3f(x0, y1, z1);

    // Bottom
    glVertex3f(x0, y0, z1);
    glVertex3f(x1, y0, z1);
    glVertex3f(x1, y0, z0);
    glVertex3f(x0, y0, z0);

    glEnd();
}

static void RenderWorld()
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    int playerCX = FloorDiv((int)std::floor(player.x), CHUNK_SIZE);
    int playerCZ = FloorDiv((int)std::floor(player.z), CHUNK_SIZE);

    constexpr int RENDER_DISTANCE = 5;

    int minX = playerCX * CHUNK_SIZE - RENDER_DISTANCE * CHUNK_SIZE;
    int maxX = playerCX * CHUNK_SIZE + (RENDER_DISTANCE + 1) * CHUNK_SIZE;

    int minZ = playerCZ * CHUNK_SIZE - RENDER_DISTANCE * CHUNK_SIZE;
    int maxZ = playerCZ * CHUNK_SIZE + (RENDER_DISTANCE + 1) * CHUNK_SIZE;

    int minY = std::max(0, (int)std::floor(player.y) - 32);
    int maxY = std::min(WORLD_HEIGHT - 1, (int)std::floor(player.y) + 32);

    for (int x = minX; x < maxX; ++x)
    {
        for (int z = minZ; z < maxZ; ++z)
        {
            for (int y = minY; y <= maxY; ++y)
            {
                uint16_t id = world->GetBlock(x, y, z);

                if (id == AIR)
                    continue;

                if (id == WATER)
                {
                    // Keep water simple and translucent.
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }

                DrawCube((float)x, (float)y, (float)z, id);

                if (id == WATER)
                    glDisable(GL_BLEND);
            }
        }
    }
}

// ============================================================
// Entity rendering
// ============================================================

static void RenderMobs()
{
    for (const auto& mob : mobs)
    {
        if (!mob.alive)
            continue;

        glColor3f(0.65f, 0.25f, 0.75f);

        glPushMatrix();
        glTranslatef(mob.x, mob.y, mob.z);
        glScalef(0.7f, 1.8f, 0.7f);

        glBegin(GL_QUADS);

        glVertex3f(-0.5f, 0, -0.5f);
        glVertex3f( 0.5f, 0, -0.5f);
        glVertex3f( 0.5f, 1, -0.5f);
        glVertex3f(-0.5f, 1, -0.5f);

        glVertex3f(-0.5f, 0,  0.5f);
        glVertex3f( 0.5f, 0,  0.5f);
        glVertex3f( 0.5f, 1,  0.5f);
        glVertex3f(-0.5f, 1,  0.5f);

        glVertex3f(-0.5f, 0, -0.5f);
        glVertex3f(-0.5f, 0,  0.5f);
        glVertex3f(-0.5f, 1,  0.5f);
        glVertex3f(-0.5f, 1, -0.5f);

        glVertex3f(0.5f, 0, -0.5f);
        glVertex3f(0.5f, 0,  0.5f);
        glVertex3f(0.5f, 1,  0.5f);
        glVertex3f(0.5f, 1, -0.5f);

        glVertex3f(-0.5f, 1, -0.5f);
        glVertex3f( 0.5f, 1, -0.5f);
        glVertex3f( 0.5f, 1,  0.5f);
        glVertex3f(-0.5f, 1,  0.5f);

        glEnd();

        glPopMatrix();
    }
}

// ============================================================
// Inventory GUI
// ============================================================

static void DrawItemIcon(float x, float y, const InventorySlot& slot)
{
    if (slot.itemID == 0 || slot.count <= 0)
        return;

    const ItemDefinition* item = GetItem(slot.itemID);

    if (!item)
        return;

    float r = 0.65f;
    float g = 0.65f;
    float b = 0.65f;

    switch (item->type)
    {
        case ItemClass::BLOCK:
            r = 0.30f; g = 0.70f; b = 0.35f;
            break;

        case ItemClass::WEAPON:
            r = 0.75f; g = 0.75f; b = 0.78f;
            break;

        case ItemClass::TOOL:
            r = 0.55f; g = 0.58f; b = 0.62f;
            break;

        case ItemClass::FOOD:
            r = 0.85f; g = 0.35f; b = 0.18f;
            break;

        case ItemClass::ARMOR:
            r = 0.25f; g = 0.65f; b = 0.90f;
            break;

        case ItemClass::MATERIAL:
            r = 0.80f; g = 0.70f; b = 0.25f;
            break;

        default:
            break;
    }

    DrawRect(x + 10, y + 10, 32, 32, r, g, b);
    DrawText(x + 12, y + 28, item->shortName, 0.75f);

    if (slot.count > 1)
        DrawText(x + 38, y + 55, std::to_string(slot.count), 0.75f);
}

static void DrawInventorySlot(float x, float y, const InventorySlot& slot,
                              bool selected, bool hovered)
{
    DrawRect(x, y, 54, 54,
             selected ? 0.28f : (hovered ? 0.20f : 0.10f),
             selected ? 0.40f : (hovered ? 0.30f : 0.10f),
             selected ? 0.65f : (hovered ? 0.45f : 0.12f));

    DrawOutline(x, y, 54, 54, 0.55f, 0.60f, 0.65f);

    DrawItemIcon(x, y, slot);
}

static void RenderInventory()
{
    Setup2D();

    DrawRect(170, 80, 940, 560, 0.06f, 0.07f, 0.10f, 0.96f);
    DrawOutline(170, 80, 940, 560, 0.60f, 0.65f, 0.75f);

    DrawText(200, 115, "INVENTORY", 1.5f);

    double mx, my;
    glfwGetCursorPos(window, &mx, &my);

    // Main inventory.
    float startX = 205;
    float startY = 170;
    float gap = 7;

    for (int row = 0; row < 3; ++row)
    {
        for (int col = 0; col < 9; ++col)
        {
            int index = 9 + row * 9 + col;

            float x = startX + col * (54 + gap);
            float y = startY + row * (54 + gap);

            bool hovered = PointInRect(mx, my, x, y, 54, 54);

            DrawInventorySlot(
                x, y,
                player.inventory.slots[index],
                false,
                hovered
            );
        }
    }

    // Hotbar.
    float hotbarY = 390;

    for (int i = 0; i < 9; ++i)
    {
        float x = startX + i * (54 + gap);
        bool hovered = PointInRect(mx, my, x, hotbarY, 54, 54);

        DrawInventorySlot(
            x, hotbarY,
            player.inventory.slots[i],
            i == player.inventory.selected,
            hovered
        );
    }

    // Armor.
    DrawText(760, 155, "ARMOR");

    for (int i = 0; i < 4; ++i)
    {
        float x = 760;
        float y = 175 + i * 65;

        bool hovered = PointInRect(mx, my, x, y, 54, 54);

        DrawInventorySlot(
            x, y,
            player.armor.armor[i],
            false,
            hovered
        );

        static const char* names[] =
        {
            "Helmet",
            "Chest",
            "Legs",
            "Boots"
        };

        DrawText(825, y + 32, names[i]);
    }

    const ItemDefinition* selectedItem =
        player.inventory.GetSelectedItem();

    if (selectedItem)
    {
        DrawText(200, 585, "Selected: " + selectedItem->name);
    }

    DrawText(200, 615, "Click slots to select. Armor slots are shown on the right.");
    DrawText(760, 500, "F2: close inventory");

    End2D();
}

static void RenderHUD()
{
    Setup2D();

    // Crosshair.
    if (mouseCaptured && !inventoryOpen)
    {
        DrawRect(WINDOW_W / 2 - 1, WINDOW_H / 2 - 8, 2, 16, 1, 1, 1);
        DrawRect(WINDOW_W / 2 - 8, WINDOW_H / 2 - 1, 16, 2, 1, 1, 1);
    }

    // Health.
    DrawText(20, 30, "Health: " + std::to_string((int)player.stats.health));
    DrawText(20, 50, "Hunger: " + std::to_string((int)player.stats.hunger));
    DrawText(20, 70, "Adrenaline: " + std::to_string((int)player.stats.adrenaline));
    DrawText(20, 90, "Strength x" + std::to_string(player.stats.StrengthMultiplier()).substr(0, 4));
    DrawText(20, 110, "Armor: " + std::to_string(player.armor.GetProtection()));

    DrawText(20, 140,
             "World: " + selectedWorldName);

    // Hotbar.
    float startX = 410;
    float y = 625;

    for (int i = 0; i < 9; ++i)
    {
        float x = startX + i * 54;

        DrawRect(x, y, 50, 50,
                 i == player.inventory.selected ? 0.30f : 0.08f,
                 i == player.inventory.selected ? 0.45f : 0.08f,
                 i == player.inventory.selected ? 0.70f : 0.08f);

        DrawOutline(x, y, 50, 50, 0.55f, 0.60f, 0.65f);

        DrawItemIcon(x - 1, y - 1, player.inventory.slots[i]);
        DrawText(x + 3, y + 65, std::to_string(i + 1), 0.75f);
    }

    End2D();
}

// ============================================================
// Input
// ============================================================

static void PerformAttack()
{
    RaycastHit hit = RaycastWorld();

    if (!hit.hit)
        return;

    if (hit.entityHit && hit.entity)
    {
        const InventorySlot& slot = *player.inventory.GetSelected();
        const ItemDefinition* item = GetItem(slot.itemID);

        float damage = 1.0f;

        if (item && item->type == ItemClass::WEAPON)
            damage = (float)item->damage;

        damage *= player.stats.StrengthMultiplier();

        hit.entity->Damage(damage);
        player.stats.adrenaline =
            std::min(100.0f, player.stats.adrenaline + 5.0f);

        return;
    }

    uint16_t broken = world->GetBlock(hit.x, hit.y, hit.z);

    world->BreakBlock(hit.x, hit.y, hit.z);

    // Basic drops.
    int dropItem = 0;

    switch (broken)
    {
        case STONE:        dropItem = 1; break;
        case GRASS:        dropItem = 2; break;
        case DIRT:         dropItem = 3; break;
        case SAND:         dropItem = 4; break;
        case SNOW:         dropItem = 5; break;
        case COBBLESTONE:  dropItem = 6; break;
        case PLANKS:       dropItem = 7; break;
        case GLASS:        dropItem = 8; break;
        case GRAVEL:       dropItem = 9; break;
        case WOOD:         dropItem = 10; break;
        case LEAVES:       dropItem = 11; break;
        case AUTUMN_LEAVES:dropItem = 12; break;
        case CACTUS:       dropItem = 13; break;
        case COAL_ORE:     dropItem = 51; break;
        case IRON_ORE:     dropItem = 52; break;
        case GOLD_ORE:     dropItem = 53; break;
        case DIAMOND_ORE:  dropItem = 54; break;
        case EMERALD_ORE:  dropItem = 55; break;
        case REDSTONE_ORE: dropItem = 51; break;
        default: break;
    }

    if (dropItem)
        player.inventory.AddItem(dropItem, 1);
}

static void PerformPlace()
{
    RaycastHit hit = RaycastWorld();

    if (!hit.hit)
        return;

    if (hit.entityHit && hit.entity)
    {
        hit.entity->Interact();
        return;
    }

    uint16_t block = player.inventory.GetSelectedBlock();

    if (block == AIR)
        return;

    int px = hit.x + hit.nx;
    int py = hit.y + hit.ny;
    int pz = hit.z + hit.nz;

    if (py < 0 || py >= WORLD_HEIGHT)
        return;

    // Do not place inside the player.
    if (IsPlayerInsideSolid(px + 0.5f, py, pz + 0.5f))
        return;

    world->SetBlock(px, py, pz, block);
    player.inventory.ConsumeSelected();
}

static void MouseButtonPressed(int button)
{
    if (gameState != GameState::PLAYING)
        return;

    if (inventoryOpen)
    {
        if (button == GLFW_MOUSE_BUTTON_LEFT)
        {
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);

            float startX = 205;
            float startY = 170;
            float gap = 7;

            // Main inventory.
            for (int row = 0; row < 3; ++row)
            {
                for (int col = 0; col < 9; ++col)
                {
                    int index = 9 + row * 9 + col;

                    float x = startX + col * (54 + gap);
                    float y = startY + row * (54 + gap);

                    if (PointInRect(mx, my, x, y, 54, 54))
                    {
                        player.inventory.selected = index;
                        return;
                    }
                }
            }

            // Hotbar.
            float hotbarY = 390;

            for (int i = 0; i < 9; ++i)
            {
                float x = startX + i * (54 + gap);

                if (PointInRect(mx, my, x, hotbarY, 54, 54))
                {
                    player.inventory.selected = i;
                    return;
                }
            }

            // Armor.
            for (int i = 0; i < 4; ++i)
            {
                float x = 760;
                float y = 175 + i * 65;

                if (PointInRect(mx, my, x, y, 54, 54))
                {
                    player.armor.Unequip(player.inventory, i);
                    return;
                }
            }
        }

        return;
    }

    if (!mouseCaptured)
    {
        mouseCaptured = true;
        firstMouse = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        return;
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT)
        PerformAttack();

    if (button == GLFW_MOUSE_BUTTON_RIGHT)
        PerformPlace();
}

static void KeyCallback(
    GLFWwindow* win,
    int key,
    int scancode,
    int action,
    int mods)
{
    if (action != GLFW_PRESS)
        return;

    if (gameState == GameState::CREATE_WORLD)
    {
        if (key == GLFW_KEY_BACKSPACE)
        {
            if (!selectedWorldName.empty())
                selectedWorldName.pop_back();

            return;
        }

        if (key == GLFW_KEY_ENTER)
        {
            // Mouse button logic will create the world.
            return;
        }

        if (key == GLFW_KEY_ESCAPE)
        {
            gameState = GameState::TITLE;
            selectedWorldName = "World";
            return;
        }

        return;
    }

    if (gameState != GameState::PLAYING)
        return;

    if (key == GLFW_KEY_ESCAPE)
    {
        if (inventoryOpen)
        {
            inventoryOpen = false;
            mouseCaptured = true;
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        else
        {
            mouseCaptured = !mouseCaptured;

            glfwSetInputMode(
                win,
                GLFW_CURSOR,
                mouseCaptured ? GLFW_CURSOR_DISABLED
                              : GLFW_CURSOR_NORMAL
            );

            firstMouse = true;
        }

        return;
    }

    if (key == GLFW_KEY_F2)
    {
        inventoryOpen = !inventoryOpen;
        mouseCaptured = !inventoryOpen;

        glfwSetInputMode(
            win,
            GLFW_CURSOR,
            mouseCaptured ? GLFW_CURSOR_DISABLED
                          : GLFW_CURSOR_NORMAL
        );

        firstMouse = true;
        return;
    }

    if (key == GLFW_KEY_F5)
    {
        firstPerson = !firstPerson;
        return;
    }

    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9)
    {
        player.inventory.Select(key - GLFW_KEY_1);
        return;
    }
}

static void CharCallback(GLFWwindow*, unsigned int codepoint)
{
    if (gameState != GameState::CREATE_WORLD)
        return;

    if (codepoint >= 32 && codepoint <= 126)
    {
        if (selectedWorldName.size() < 28)
            selectedWorldName += (char)codepoint;
    }
}

static void CursorCallback(GLFWwindow*, double xpos, double ypos)
{
    if (gameState != GameState::PLAYING)
        return;

    if (!mouseCaptured || inventoryOpen)
        return;

    if (firstMouse)
    {
        lastMouseX = xpos;
        lastMouseY = ypos;
        firstMouse = false;
        return;
    }

    double dx = xpos - lastMouseX;
    double dy = ypos - lastMouseY;

    lastMouseX = xpos;
    lastMouseY = ypos;

    player.yaw += (float)dx * mouseSensitivity;
    player.pitch += (float)dy * mouseSensitivity;

    player.pitch = std::clamp(player.pitch, -1.55f, 1.55f);
}

static void ScrollCallback(GLFWwindow*, double, double yoffset)
{
    if (gameState != GameState::PLAYING || inventoryOpen)
        return;

    if (yoffset > 0)
        scrollDelta++;

    if (yoffset < 0)
        scrollDelta--;
}

// ============================================================
// Camera
// ============================================================

static void SetupCamera()
{
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    float aspect = height == 0 ? 1.0f : (float)width / (float)height;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    gluPerspective(70.0, aspect, 0.05, 1000.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float dx, dy, dz;
    GetLookVector(dx, dy, dz);

    if (firstPerson)
    {
        float eyeX = player.x;
        float eyeY = player.y + 1.62f;
        float eyeZ = player.z;

        gluLookAt(
            eyeX, eyeY, eyeZ,
            eyeX + dx,
            eyeY + dy,
            eyeZ + dz,
            0, 1, 0
        );
    }
    else
    {
        float cameraDistance = 5.0f;

        float cx = player.x - dx * cameraDistance;
        float cy = player.y + 1.4f - dy * cameraDistance;
        float cz = player.z - dz * cameraDistance;

        gluLookAt(
            cx, cy, cz,
            player.x,
            player.y + 1.2f,
            player.z,
            0, 1, 0
        );
    }
}

// ============================================================
// Game initialization
// ============================================================

static void CreateDefaultWorld()
{
    currentWorldPath = "saves/World";
    selectedWorldName = "World";

    fs::create_directories(currentWorldPath);

    if (!fs::exists(currentWorldPath + "/world.dat"))
    {
        std::random_device rd;
        worldSeed = ((uint64_t)rd() << 32) ^ rd();

        std::ofstream out(currentWorldPath + "/world.dat");
        out << "VOXELCRAFT_WORLD\n";
        out << "seed " << worldSeed << "\n";
        out << "version 1\n";
    }

    world = new World(currentWorldPath);
}

static void StartNewWorld()
{
    gameState = GameState::TITLE;
    mouseCaptured = false;

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

// ============================================================
// Main rendering
// ============================================================

static void RenderPlaying()
{
    glClearColor(0.52f, 0.72f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    SetupCamera();

    RenderWorld();
    RenderMobs();

    if (!inventoryOpen)
        RenderHUD();
    else
        RenderInventory();
}

static void Render()
{
    if (gameState == GameState::TITLE)
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RenderTitleScreen();
        return;
    }

    if (gameState == GameState::CREATE_WORLD)
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RenderCreateWorldScreen();
        return;
    }

    if (gameState == GameState::PLAYING)
    {
        RenderPlaying();
        return;
    }
}

// ============================================================
// Main
// ============================================================

int main()
{
    InitItems();

    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW.\n";
        return 1;
    }

    window = glfwCreateWindow(
        WINDOW_W,
        WINDOW_H,
        "VoxelCraft",
        nullptr,
        nullptr
    );

    if (!window)
    {
        std::cerr << "Failed to create GLFW window.\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    int argc = 1;
    char arg0[] = "VoxelCraft";
    char* argv[] = { arg0, nullptr };
    glutInit(&argc, argv);

    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCharCallback(window, CharCallback);
    glfwSetCursorPosCallback(window, CursorCallback);
    glfwSetScrollCallback(window, ScrollCallback);

    glfwSetMouseButtonCallback(
        window,
        [](GLFWwindow*, int button, int action, int)
        {
            if (action == GLFW_PRESS)
                MouseButtonPressed(button);
        }
    );

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    CreateDefaultWorld();
    delete world;
    world = nullptr;

    double lastTime = glfwGetTime();
    double saveTimer = 0.0;

    while (!glfwWindowShouldClose(window))
    {
        double now = glfwGetTime();
        float dt = (float)(now - lastTime);
        lastTime = now;

        dt = std::min(dt, 0.05f);

        glfwPollEvents();

        if (gameState == GameState::PLAYING)
        {
            if (scrollDelta != 0)
            {
                int newSlot =
                    player.inventory.selected -
                    scrollDelta;

                while (newSlot < 0)
                    newSlot += Inventory::HOTBAR;

                while (newSlot >= Inventory::HOTBAR)
                    newSlot -= Inventory::HOTBAR;

                player.inventory.selected = newSlot;
                scrollDelta = 0;
            }

            UpdatePlayer(dt);

            saveTimer += dt;

            if (saveTimer >= 30.0)
            {
                if (world)
                    world->SaveAll();

                saveTimer = 0.0;
            }
        }

        Render();

        glfwSwapBuffers(window);
    }

    if (world)
    {
        world->SaveAll();
        delete world;
        world = nullptr;
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
