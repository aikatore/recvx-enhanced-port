/* Afterimages of a record drawn as another model (docs/ACTORS.md "Afterimages").
 *
 * Wesker's fast moves leave a trail (effsub1b.c bhEff135/136; the scripts of 1002, 9321, 9370 and the Battle Game's
 * 5820 link it to his record): each frame the game copies the skinned vertices of one object of the linked work (his
 * body, and his gloved fist, a hand piece) and draws that copy for five frames, fading (alpha 200 down to 40). A
 * record drawn as another model (the cutscene cast, the double of the character played) left Wesker's trail behind
 * that model. Here the trail is the model's: at each copy its node matrices are kept (actor_drawn_for: posed from the
 * record as its drawing poses it) and the model is drawn again at them, fading as the game's copy. A hand piece the
 * model hides (it has hands of its own: cast_glue.c hands_hide) leaves none. The game's copy is still made: an
 * afterimage whose entry was taken by a newer one is drawn from it (Wesker), never from a stale buffer.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../src/host/actors.h"
#include <string.h>

int actor_drawn_for(BH_PWORK* ep);                                   /* actor_glue.c */
void actor_draw_nodes(int who, const NJS_MATRIX* m, int n);
extern unsigned int _nj_control_3d_flag_;
void njControl3D(unsigned int flag);                                 /* the Ninja library (ps2_NinjaCnk.c) */
void njSetConstantAttr(unsigned int and_attr, unsigned int or_attr);

#define TRAILS 32                       /* (two works with a trail, six afterimages each, at most) */
#define NODES 128
typedef struct {
    const O_WRK* op;                    /* the afterimage (an effect 136) */
    short who, model;                   /* the display actor and its model then; who -1: nothing drawn */
    int n;
    NJS_MATRIX m[NODES];
} Trail;
static Trail tr[TRAILS];
static int next;

/* bhEff136, at its copy */
void pc_trail_grab(O_WRK* op)
{
    BH_PWORK *ep = (BH_PWORK*)op->lkwkp, *w;
    Trail* t;
    int i, who;
    for (i = 0; i < TRAILS; i++) if (tr[i].op == op) tr[i].op = NULL;   /* (the effect's last afterimage) */
    if (!ep) return;
    if ((who = actor_drawn_for(ep)) < 0) {   /* a piece linked to a record drawn as another model that hides it: none */
        BH_PWORK* l = (ep->flg & 0x80) ? (BH_PWORK*)ep->lkwkp : NULL;
        if (!l || !(ep->mdl[op->mdlver].objP[op->lkono].evalflags & 0x8) || actor_drawn_for(l) < 0) return;
    }
    t = &tr[next];
    next = (next + 1) % TRAILS;
    t->op = op;
    t->who = -1;
    if (who < 0 || !(w = actor_work(who)) || w->mlwP->obj_num > NODES) return;
    t->who = (short)who;
    t->model = (short)actor_shown_model(who);
    t->n = w->mlwP->obj_num;
    for (i = 0; i < t->n; i++) memcpy(t->m[i], w->mlwP->owP[i].mtx, sizeof(NJS_MATRIX));
}

/* the materials' alpha of n objects (npSetAllMatAlphaColor's walk): set to a, the old ones kept in s; a < 0: put back */
static int mat_alpha(NJS_CNK_OBJECT* o, int n, unsigned char* s, int cap, int a)
{
    int k = 0;
    for (; n > 0; n--, o++) {
        short* p;
        if (!o->model || (o->evalflags & 0x8)) continue;
        for (p = o->model->plist;;) {
            int head = (unsigned char)*p++;
            if (head == 255) break;
            if (head == 8) { p++; continue; }
            if (head >= 17 && head < 24) {
                unsigned char* mat = (unsigned char*)p + 5;
                int j, c = (head == 17 || head == 21) ? 1 : (head == 19 || head == 23) ? 2 : 0;
                for (j = 0; j < c && k < cap; j++, k++, mat += 4) {
                    if (a >= 0) { s[k] = *mat; *mat = (unsigned char)a; }
                    else *mat = s[k];
                }
            }
            if ((head >= 17 && head < 24) || (head >= 56 && head < 59) || (head >= 64 && head < 67)) p += 1 + *p;
        }
    }
    return k;
}

/* bhDraw136: 1 = drawn here (or nothing), 0 = the game's copy */
int pc_trail_draw(O_WRK* op)
{
    static unsigned char keep[4096];
    BH_PWORK* w;
    int i, tp;
    unsigned int c3d;
    for (i = 0; i < TRAILS && tr[i].op != op; i++);
    if (i == TRAILS) return 0;
    if (tr[i].who < 0 || !(w = actor_work(tr[i].who)) || actor_shown_model(tr[i].who) != tr[i].model || w->mlwP->obj_num != tr[i].n) return 1;
    njPushMatrixEx();
    c3d = _nj_control_3d_flag_;
    njControl3D(0x800);   /* (as the game's: its materials' alpha) */
    njSetConstantAttr(0, 0xA00);
    njCnkSetSimpleLightColor(1.0f, 1.0f, 1.0f);
    njCnkSetSimpleLightIntensity(1.0f, 0.8f);
    njCnkSetSimpleLight(0, 0.3f, 0.7f);
    mat_alpha(w->mlwP->objP, tr[i].n, keep, sizeof(keep), (op->ct0 + 1) * 40);
    tp = w->draw_tp;
    w->draw_tp = 3;   /* (njCnkSimpleDrawModel, as the game's) */
    actor_draw_nodes(tr[i].who, (const NJS_MATRIX*)tr[i].m, tr[i].n);
    w->draw_tp = tp;
    mat_alpha(w->mlwP->objP, tr[i].n, keep, sizeof(keep), -1);
    njControl3D(c3d);
    njPopMatrixEx();
    return 1;
}
