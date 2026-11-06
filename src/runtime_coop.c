/* BloodOnline: the Yharnam Together module's (gpu/ytcoop) calls into the runtime's own paths: the
 * pad (party death, the cutscene vote, the team menu's Circle), the sound held under the team's
 * loading screen, the save file the character plays and trophies. Each is the module's bbcoop_*
 * function when that module is the online module, else nothing happens. The renderer's overlay
 * reaches the rest of bbcoop.h through runtime_net_module_symbol. */
#include "runtime.h"
#include <stdint.h>

typedef void (*PadFilter)(uint32_t *buttons, uint8_t sticks[4], uint8_t *l2, uint8_t *r2);
typedef int (*IntFn)(void);
typedef int (*VoteFn)(int options);
typedef void (*PathFn)(const char *path);
typedef void (*TrophyFn)(int32_t id);

static struct {
    int resolved;
    PadFilter party_death_pad;
    IntFn cutscene_skip, team_menu_back, sound_held;
    VoteFn cutscene_vote_pad;
    PathFn file_opened;
    TrophyFn trophy_unlocked;
} coop;

/* Resolved once the online module is loaded (on the game's first network or PSN import). */
static int ready(void) {
    if (__atomic_load_n(&coop.resolved,__ATOMIC_ACQUIRE)) return coop.party_death_pad!=0;
    if (!runtime_net_module_symbol("bbnet_init")) return 0;
    coop.party_death_pad=(PadFilter)runtime_net_module_symbol("bbcoop_party_death_pad");
    coop.cutscene_skip=(IntFn)runtime_net_module_symbol("bbcoop_cutscene_skip");
    coop.team_menu_back=(IntFn)runtime_net_module_symbol("bbcoop_team_menu_back");
    coop.sound_held=(IntFn)runtime_net_module_symbol("bbcoop_sound_held");
    coop.cutscene_vote_pad=(VoteFn)runtime_net_module_symbol("bbcoop_cutscene_vote_pad");
    coop.file_opened=(PathFn)runtime_net_module_symbol("bbcoop_file_opened");
    coop.trophy_unlocked=(TrophyFn)runtime_net_module_symbol("bbcoop_trophy_unlocked");
    __atomic_store_n(&coop.resolved,1,__ATOMIC_RELEASE);
    return coop.party_death_pad!=0;
}

void runtime_coop_pad(uint32_t *buttons, uint8_t sticks[4], uint8_t *l2, uint8_t *r2) {
    if (!ready()) return;
    coop.party_death_pad(buttons,sticks,l2,r2);
    /* Under a party's cutscene Options is a vote; the skip waits for everyone watching. */
    if (coop.cutscene_vote_pad) {
        const int vote=coop.cutscene_vote_pad((*buttons & RUNTIME_PAD_OPTIONS)!=0);
        if (vote==1) *buttons&=~(uint32_t)RUNTIME_PAD_OPTIONS;
        else if (vote==2) *buttons|=RUNTIME_PAD_OPTIONS;
    }
    runtime_coop_pad_captured(buttons);
}

void runtime_coop_pad_captured(uint32_t *buttons) {
    if (!ready()) return;
    /* A cutscene under the team's loading screen skips by itself; the team screen over the game
     * closed: the game's page under it too. */
    if (coop.cutscene_skip && coop.cutscene_skip()) *buttons|=RUNTIME_PAD_OPTIONS;
    if (coop.team_menu_back && coop.team_menu_back()) *buttons|=RUNTIME_PAD_CIRCLE;
}

int runtime_coop_sound_held(void) {
    return ready() && coop.sound_held ? coop.sound_held() : 0;
}

void runtime_coop_file_opened(const char *guest_path) {
    if (guest_path && ready() && coop.file_opened) coop.file_opened(guest_path);
}

void runtime_coop_trophy_unlocked(int32_t id) {
    if (ready() && coop.trophy_unlocked) coop.trophy_unlocked(id);
}
