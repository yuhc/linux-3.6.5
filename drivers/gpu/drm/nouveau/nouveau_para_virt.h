#ifndef __NOUVEAU_PARA_VIRT_H__
#define __NOUVEAU_PARA_VIRT_H__

#define NOUVEAU_PV_REG_BAR 4
#define NOUVEAU_PV_SLOT_SIZE 0x1000ULL
#define NOUVEAU_PV_SLOT_NUM 64ULL
#define NOUVEAU_PV_SLOT_TOTAL (NOUVEAU_PV_SLOT_SIZE * NOUVEAU_PV_SLOT_NUM)
#define NOUVEAU_PV_BATCH_SIZE 128ULL

/* PV OPS */
enum {
	NOUVEAU_PV_OP_SET_PGD,
	NOUVEAU_PV_OP_MAP_PGT,
	NOUVEAU_PV_OP_MAP,
	NOUVEAU_PV_OP_MAP_BATCH,
	NOUVEAU_PV_OP_MAP_SG_BATCH,
	NOUVEAU_PV_OP_UNMAP_BATCH,
	NOUVEAU_PV_OP_VM_FLUSH,
	NOUVEAU_PV_OP_MEM_ALLOC,
	NOUVEAU_PV_OP_MEM_FREE,
	NOUVEAU_PV_OP_BAR3_PGT,
	NOUVEAU_PV_OP_MAP_PGT_BATCH,
};

struct nouveau_para_virt_slot {
	union {
		u8   u8[NOUVEAU_PV_SLOT_SIZE / sizeof(u8) ];
		u16 u16[NOUVEAU_PV_SLOT_SIZE / sizeof(u16)];
		u32 u32[NOUVEAU_PV_SLOT_SIZE / sizeof(u32)];
		u64 u64[NOUVEAU_PV_SLOT_SIZE / sizeof(u64)];
	};
};

struct nouveau_para_virt_mem {
	struct drm_device *dev;
	struct kref refcount;
	u32 id;
	u32 size;
};

struct nouveau_channel;
struct nouveau_vma;
struct nouveau_mem;

int  nouveau_para_virt_init(struct drm_device *);
void nouveau_para_virt_takedown(struct drm_device *);
struct nouveau_para_virt_slot* nouveau_para_virt_alloc_slot(struct drm_device *);
void nouveau_para_virt_free_slot(struct drm_device *, struct nouveau_para_virt_slot *);
int nouveau_para_virt_call(struct drm_device *, struct nouveau_para_virt_slot *);

int nouveau_para_virt_mem_new(struct drm_device *, u32 size, struct nouveau_para_virt_mem **);
void nouveau_para_virt_mem_ref(struct nouveau_para_virt_mem *, struct nouveau_para_virt_mem **);

int nouveau_para_virt_set_pgd(struct nouveau_channel*, struct nouveau_para_virt_mem*, int id);
int nouvaeu_para_virt_map_pgt(struct nouveau_para_virt_mem *pgd, u32 index, struct nouveau_para_virt_mem *pgt[2]);
int nouvaeu_para_virt_map_pgt_batch(struct nouveau_para_virt_mem *pgd, struct nouveau_vm *vm);
int nouveau_para_virt_map(struct nouveau_para_virt_mem *pgt, u32 index, u64 phys);
int nouveau_para_virt_map_batch(struct nouveau_para_virt_mem *pgt, u32 index, u64 phys, u32 next, u32 count);
int nouveau_para_virt_unmap_batch(struct nouveau_para_virt_mem *pgt, u32 index, u32 count);
int nouveau_para_virt_map_sg_batch(struct nouveau_para_virt_mem *pgt, u32 index, struct nouveau_vma *vma, struct nouveau_mem *mem, dma_addr_t *list, u32 count);
int nouveau_para_virt_vm_flush(struct nouveau_para_virt_mem* pgd, u32 engine);
int nouveau_para_virt_bar3_pgt(struct nouveau_para_virt_mem *pgt);

#endif
