#ifndef __NOUVEAU_PARA_VIRT_H__
#define __NOUVEAU_PARA_VIRT_H__

#define NOUVEAU_PV_REG_BAR 4
#define NOUVEAU_PV_SLOT_SIZE 0x100ULL
#define NOUVEAU_PV_SLOT_NUM 64ULL
#define NOUVEAU_PV_SLOT_TOTAL (NOUVEAU_PV_SLOT_SIZE * NOUVEAU_PV_SLOT_NUM)

/* PV OPS */
enum {
	NOUVEAU_PV_OP_PGD_SET,
	NOUVEAU_PV_OP_PGD_SET,
};

struct nouveau_para_virt_slot {
	union {
		u8   u8[NOUVEAU_PV_SLOT_SIZE / sizeof(u8) ];
		u16 u16[NOUVEAU_PV_SLOT_SIZE / sizeof(u16)];
		u32 u32[NOUVEAU_PV_SLOT_SIZE / sizeof(u32)];
		u64 u64[NOUVEAU_PV_SLOT_SIZE / sizeof(u64)];
	};
};

int  nouveau_para_virt_init(struct drm_device *);
void nouveau_para_virt_takedown(struct drm_device *);
struct nouveau_para_virt_slot* nouveau_para_virt_alloc_slot(struct drm_device *);
void nouveau_para_virt_free_slot(struct drm_device *, struct nouveau_para_virt_slot *);
int nouveau_para_virt_call(struct drm_device *, struct nouveau_para_virt_slot *);

#endif
