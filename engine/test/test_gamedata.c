// SRCS: -Ifake
/* gamedata.c's name lookups (hash tables) on a small GDAT: the same answers as the walks they replaced */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/gamedata.h"

const char *game_stage_name = "TestBGalaxy/1";

typedef struct { char name[64]; u32 progress; } GStory;
typedef struct { char name[32]; u8 open_stars, grand, pad[2]; char cond[3][40]; char pane[16]; } GInfo;
typedef struct { u8 star_mask, hidden, pad[2]; char comet[12]; u32 luigi; } GScen1;
typedef struct { char galaxy[32]; u8 n, pad[3]; GScen1 s[8]; } GScen;
static struct { u32 n[4]; GStory st[1]; GInfo in[3]; GScen sc[3]; } g = {{1, 3, 3, 0}};

int main(void)
{
    const char *names[3] = {"TestAGalaxy", "TestBGalaxy", "TestCGalaxy"};
    for (int i = 0; i < 3; i++) {
        strcpy(g.in[i].name, names[i]); g.in[i].open_stars = (u8)(i * 5);
        strcpy(g.sc[i].galaxy, names[i]); g.sc[i].n = (u8)(i + 1);
        for (int s = 0; s <= i; s++) g.sc[i].s[s].star_mask = 1;
    }
    Scene sc;
    memset(&sc, 0, sizeof sc);
    sc.gdat = (const u8 *)&g;
    gd_attach(&sc);
    for (int i = 0; i < 3; i++) {
        assert(gd_scenario_num(names[i]) == i + 1);
        assert(gd_power_star_num(names[i]) == i + 1);
        assert(gd_stars_to_open(names[i]) == i * 5);
    }
    assert(gd_scenario_num("NoGalaxy") == 0 && gd_star_num_galaxy("NoGalaxy") == 0);
    gd_set_star("TestCGalaxy", 2); gd_set_star("TestAGalaxy", 1); gd_set_star("TestCGalaxy", 3);
    assert(gd_has_star("TestCGalaxy", 2) && !gd_has_star("TestCGalaxy", 1) && gd_star_num() == 3);
    assert(gd_flag_known("AppearFlipPanelExGalaxy") && gd_flag_known("SpecialStarGrand2") && !gd_flag_known("NoSuchFlag"));
    assert(!strcmp(gd_flag_galaxy("AppearFlipPanelExGalaxy"), "FlipPanelExGalaxy"));
    gd_reset(); /* the galaxies' names again: their table rebuilt by name */
    assert(gd_star_num() == 0 && !gd_has_star("TestCGalaxy", 2));
    gd_set_star("TestBGalaxy", 2);
    assert(gd_star_num() == 1 && gd_has_star("TestBGalaxy", 2));
    printf("gamedata: ok\n");
    return 0;
}
