#include "CharacterModule.h"
#include "CharacterController.h"
#include "CharacterModule.h"
#include "CharacterController.h"

int main()
{
    CharacterModule player = {};
    CharacterController controller = {};
    CharacterConfig config = character_default_config();
    CharacterControllerConfig pair = character_controller_default_config();
    character_update(&player, 0U);
    character_controller_update(&controller, 0U, false);
    return config.encoder_sign == 1 && pair.enter_x10 == 50U
        && !character_is_calibrated(&player) ? 0 : 1;
}
