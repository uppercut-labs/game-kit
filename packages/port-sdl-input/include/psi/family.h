/* The controller family button prompts follow. A port's renderer can hold this without SDL. */
#ifndef PSI_FAMILY_H
#define PSI_FAMILY_H

#include <string.h>

typedef enum {
    PSI_FAMILY_PLAYSTATION,   /* DualShock 3/4, DualSense */
    PSI_FAMILY_XBOX,          /* Xbox 360 / One / Series and other XInput pads */
    PSI_FAMILY_STEAM,         /* Steam Deck, Steam Controller */
    PSI_FAMILY_OTHER,         /* other gamepads, joysticks and arcade sticks */
    PSI_FAMILY_KEYBOARD,
    PSI_FAMILY_NINTENDO,      /* Switch Pro, Joy-Con */
    PSI_FAMILY_COUNT
} PsiFamily;

static inline const char *psi_family_name(PsiFamily f)
{
    static const char *const N[] = { "playstation", "xbox", "steam", "other", "keyboard", "nintendo" };

    return (unsigned)f < PSI_FAMILY_COUNT ? N[f] : "other";
}

/* The family a name says ("xbox"), or -1 if it is not one. */
static inline int psi_family_parse(const char *name)
{
    int f;

    for (f = 0; f < PSI_FAMILY_COUNT; f++) {
        if (!strcmp(name, psi_family_name((PsiFamily)f))) {
            return f;
        }
    }
    return -1;
}

#endif
