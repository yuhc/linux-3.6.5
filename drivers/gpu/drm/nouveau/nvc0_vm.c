/*
 * Copyright 2010 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_vm.h"
#include "nouveau_para_virt.h"

void
nvc0_vm_map_pgt(struct nouveau_para_virt_mem *pgd, u32 index,
		struct nouveau_para_virt_mem *pgt[2])
{
	nouvaeu_para_virt_map_pgt(pgd, index, pgt);
}

static inline u64
nvc0_vm_addr(struct nouveau_vma *vma, u64 phys, u32 memtype, u32 target)
{
	phys >>= 8;

	phys |= 0x00000001; /* present */
	if (vma->access & NV_MEM_ACCESS_SYS)
		phys |= 0x00000002;

	phys |= ((u64)target  << 32);
	phys |= ((u64)memtype << 36);

	return phys;
}

void
nvc0_vm_map(struct nouveau_vma *vma, struct nouveau_para_virt_mem *pgt,
	    struct nouveau_mem *mem, u32 pte, u32 cnt, u64 phys, u64 delta)
{
	u32 next = 1 << (vma->node->type - 8);
	phys  = nvc0_vm_addr(vma, phys, mem->memtype, 0);
	nouveau_para_virt_map_batch(pgt, pte, phys, next, cnt);
}

void
nvc0_vm_map_sg(struct nouveau_vma *vma, struct nouveau_para_virt_mem *pgt,
	       struct nouveau_mem *mem, u32 pte, u32 cnt, dma_addr_t *list)
{
	nouveau_para_virt_map_sg_batch(pgt, pte, vma, mem, list, cnt);
}

void
nvc0_vm_unmap(struct nouveau_para_virt_mem *pgt, u32 pte, u32 cnt)
{
	nouveau_para_virt_unmap_batch(pgt, pte, cnt);
}

void
nvc0_vm_flush(struct nouveau_vm *vm)
{
	struct drm_nouveau_private *dev_priv = vm->dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct nouveau_vm_pgd *vpgd;
	unsigned long flags;
	u32 engine;

	engine = 1;
	if (vm == dev_priv->bar1_vm || vm == dev_priv->bar3_vm)
		engine |= 4;

	pinstmem->flush(vm->dev);

	spin_lock_irqsave(&dev_priv->vm_lock, flags);
	// TODO(Yusuke Suzuki):
	// optimize it
	list_for_each_entry(vpgd, &vm->pgd_list, head) {
		nouveau_para_virt_vm_flush(vpgd->obj, engine);
	}
	spin_unlock_irqrestore(&dev_priv->vm_lock, flags);
}
