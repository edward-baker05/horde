//
// Created by Bailen on 13/08/2026.
//
#pragma once

#include <atomic>
#include <array>
#include <vector>
// TODO: test this
#define GLM_FORCE_INLINE
#include <glm/glm.hpp>

struct RenderSnapshot {
    std::vector<glm::vec2> positions;
    std::vector<int> health;
    size_t unitCount = 0;
};

// Uniform buffer structure matching shaders/src/clear_grid.comp.hlsl
struct ClearGridUniforms {
    uint32_t numCells = 0;
    uint32_t padding[3] = {0, 0, 0};
};
static_assert(sizeof(ClearGridUniforms) == 16, "ClearGridUniforms must match the HLSL cbuffer layout");

// Prefix sum and reordering uniform structures
struct ScanUniforms {
    uint32_t numCells = 0;
    uint32_t padding[3] = {0, 0, 0};
};
static_assert(sizeof(ScanUniforms) == 16, "ScanUniforms must match the HLSL cbuffer layout");

struct TopScanUniforms {
    uint32_t numBlocks = 0;
    uint32_t padding[3] = {0, 0, 0};
};
static_assert(sizeof(TopScanUniforms) == 16, "TopScanUniforms must match the HLSL cbuffer layout");

struct AddUniforms {
    uint32_t numCells = 0;
    uint32_t padding[3] = {0, 0, 0};
};
static_assert(sizeof(AddUniforms) == 16, "AddUniforms must match the HLSL cbuffer layout");

struct ReorderUniforms {
    uint32_t unitCount = 0;
    uint32_t padding[3] = {0, 0, 0};
};
static_assert(sizeof(ReorderUniforms) == 16, "ReorderUniforms must match the HLSL cbuffer layout");

// Uniform buffer structure matching shaders/src/update_positions.comp.hlsl
struct UnitSimulationUniforms {
    float deltaTime = 0.0f;
    uint32_t unitCount = 0;
    float gravity = 10.0f;
    float restitution = -0.1f;

    glm::vec2 worldOrigin{0.0f, 0.0f};
    glm::vec2 maxPosition{0.0f, 0.0f};

    glm::vec2 minVelocity{-25.0f, -25.0f};
    glm::vec2 maxVelocity{25.0f, 25.0f};

    float floorFriction = 0.85f;
    float unitSize = 5.0f;
    float cellSize = 5.0f;
    float invCellSize = 0.2f;

    float rowHeight = 4.330127f;
    float invRowHeight = 0.23094f;
    int32_t gridCols = 0;
    int32_t gridRows = 0;

    int32_t maxCol = 0;
    int32_t maxRow = 0;
    glm::vec2 padding{0.0f, 0.0f};
};

static_assert(sizeof(UnitSimulationUniforms) == 96, "UnitSimulationUniforms must match the HLSL cbuffer layout");


// Uniform buffer structure matching shaders/src/resolve_collisions.comp.hlsl
struct UnitCollisionUniforms {
    uint32_t unitCount = 0;
    float unitSize = 5.0f;
    float desiredDistSq = 25.0f;
    float cellSize = 5.0f;

    float invCellSize = 0.2f;
    float rowHeight = 4.330127f;
    float invRowHeight = 0.23094f;
    float restitution = -0.1f;

    int32_t gridCols = 0;
    int32_t gridRows = 0;
    int32_t maxCol = 0;
    int32_t maxRow = 0;

    glm::vec2 worldOrigin{0.0f, 0.0f};
    glm::vec2 maxPosition{0.0f, 0.0f};

    float damping = 0.5f;
    float maxDisplacement = 5.0f;
    uint32_t enableSleeping = 1;
    uint32_t deepSleepMinContacts = 5;

    float sleepMaxRelSpeed = 1.5f;
    float overRelaxation = 1.35f;
    float contactFriction = 0.25f;
    float padding = 0.0f;
};

static_assert(sizeof(UnitCollisionUniforms) == 96, "UnitCollisionUniforms must match the HLSL cbuffer layout");

// Uniform buffer structure matching shaders/src/unit.vert.hlsl
struct UnitRenderUniforms {
    glm::mat4 viewProjection{1.0f};
    glm::vec2 unitSize{5.0f, 5.0f};
    glm::vec2 padding0{0.0f, 0.0f};
    glm::vec4 unitUv{0.0f, 0.0f, 1.0f, 1.0f};
};

static_assert(sizeof(UnitRenderUniforms) == 96, "UnitRenderUniforms must match the HLSL cbuffer layout");



// SoA based approach
class UnitManager {
private:
    std::vector<glm::vec2> positions;
    std::vector<glm::vec2> velocities;

    std::vector<int> health;
    std::vector<float> invMass;

    float unitSize;
    float cellSize;
    float invCellSize;
    float rowHeight;
    float invRowHeight;

    size_t currentUnits = 0;
    size_t maxUnits{};

    // World bounds
    static constexpr glm::vec2 worldOrigin = {0.0f, 0.0f};
    glm::vec2 worldBounds{};
    glm::vec2 maxP{};
    float restitution = -0.1f;

    // Spatial Counting Sort grid structures
    std::vector<int> cellStarts;
    std::vector<int> cellCounts;
    std::vector<int> cellCurrent;
    std::vector<int> unitCellIndices;
    std::vector<int> activeCells;

    // Ping-pong buffers for spatial sorting
    std::vector<glm::vec2> sortedPositions;
    std::vector<glm::vec2> sortedVelocities;
    std::vector<int> sortedHealth;
    std::vector<float> sortedInvMass;

    // Triple-buffered render snapshots for lock-free async rendering
    std::array<RenderSnapshot, 3> renderSnapshots;
    std::atomic<int> readySnapshotIndex{-1};
    int writeSnapshotIndex = 0;
    int readSnapshotIndex = -1;

    // Cell status flags
    enum CellFlags : uint8_t {
        CellFlag_None = 0,
        CellFlag_InGroup = 1 << 0,       // Cell belongs to a coherent connected macro-group
        CellFlag_GroupInterior = 1 << 1, // Cell is completely surrounded by cells of the same group
    };

    std::vector<uint8_t> cellFlags;
    std::vector<int> cellGroupId;
    std::vector<glm::vec2> cellMeanVelocities;
    std::vector<uint8_t> cellIsStable;
    std::vector<uint8_t> cellStabilityTimer;
    std::vector<int> bfsQueue;
    std::vector<glm::vec2> groupVelocities;
    std::vector<int> groupUnitCounts;

    // Rotation-invariant unit-level group tracking
    std::vector<int> unitGroupId;
    std::vector<uint8_t> unitStabilityTimer;
    std::vector<uint8_t> unitContactCount;

    int gridCols = 0;
    int gridRows = 0;
    int maxCol = 0;
    int maxRow = 0;

    void BuildUniformGrid();
    void FormCellGroupsAndClassify();
    inline void PruneGroupCell(int cellIndex);

    void UpdatePositions(float dt);

    inline void ResolveEdgeCollisions(glm::vec2& pos, glm::vec2& vel) const;
    inline bool ResolveCollisionPair(int u, int v, float desiredDistSquared);
    bool ResolveRangeCollisions(int u, int start, int end, float desiredDistSquared);

    void ResolveEntityCollisions(float dt);

public:
    // Live Tuning Parameters (interactive in ImGui)
    float groupVelVarianceSq = 16.0f;   // Internal cell velocity variance threshold
    float groupNeighborDiffSq = 16.0f;  // Neighbor velocity difference threshold to merge
    float minPackingQuality = 0.90f;    // Minimum packing quality ratio (0.70 to 0.99)
    int stabilityFramesToSleep = 3;     // Consecutive stable steps before sleeping
    int solverIterations = 4;           // Solver passes per sub-step
    int physicsSubSteps = 2;            // Sub-steps per simulation tick

    // Live Sleeping Statistics
    size_t sleepingInteriorUnits = 0;
    size_t sleepingEdgeUnits = 0;
    size_t activeUnits = 0;

    // Constructor & Initialization
    UnitManager() = default;
    UnitManager(size_t MaxUnits, glm::vec2 WorldBounds, float unit_size);
    void Init(size_t MaxUnits, glm::vec2 WorldBounds, float unit_size);

    void Reserve(size_t newMaxUnits);

    void SpawnUnit(glm::vec2 pos, glm::vec2 vel, int hp, float invM = 0.01f);
    void ClearUnits();

    // Main Loop
    void UpdatePhysics(float dt);

    // Async Triple-Buffering API
    void PublishRenderSnapshot();
    const RenderSnapshot* AcquireRenderSnapshot();

    void PopulateUniformGrid();

    // Getters and Setters
    [[nodiscard]] size_t GetCurrentUnits() const;
    [[nodiscard]] size_t GetMaxUnits() const;
    [[nodiscard]] glm::vec2 GetPosition(size_t index) const;
    [[nodiscard]] const glm::vec2* GetPositionsPtr() const;
    [[nodiscard]] const glm::vec2* GetVelocitiesPtr() const { return velocities.data(); }
    [[nodiscard]] glm::vec2* GetPositionsMutablePtr() { return positions.data(); }
    [[nodiscard]] glm::vec2* GetVelocitiesMutablePtr() { return velocities.data(); }
    [[nodiscard]] const int* GetHealthsPtr() const;
    [[nodiscard]] const int* GetCellStartsPtr() const { return cellStarts.data(); }
    [[nodiscard]] size_t GetCellStartsCount() const { return cellStarts.size(); }

    [[nodiscard]] float GetCellSize() const { return cellSize; }
    [[nodiscard]] float GetRowHeight() const { return rowHeight; }
    [[nodiscard]] glm::vec2 GetWorldBounds() const { return worldBounds; }
    [[nodiscard]] int GetGridCols() const { return gridCols; }
    [[nodiscard]] int GetGridRows() const { return gridRows; }
};

