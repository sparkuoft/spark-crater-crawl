typedef struct {
    int id;             // 0-based pillar index
    bool is_hit;
    uint32_t hit_ms;
} PillarCollisionStatus;

typedef struct {
    bool is_hit;
    uint32_t hit_ms;     // millis() at detection
} CharacterCollisionStatus;

class CharacterModule {
public:
    CharacterModule();

    CharacterCollisionStatus readCharacterCollision();
    PillarCollisionStatus readPillarCollision();
    int getAngle();

    void update();

private:
    int currAngle = 0;

    PillarCollisionStatus pillar_collision_status;
    CharacterCollisionStatus character_collision_status;

    // Button state
    int buttonPressed = 0;
    int lastButtonState = HIGH;
};