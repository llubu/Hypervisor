#include <inc/lib.h>

    int
pageref(void *v)
{
    pte_t pte;

    if (!(vpd[VPD(v)] & PTE_P))
        return 0;
    pte = vpt[PPN(v)];
    if (!(pte & PTE_P))
        return 0;
    return pages[PPN(pte)].pp_ref;
}
