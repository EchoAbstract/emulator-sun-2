
/*	il.c	1.0	84/09/06	*/

/*
 * Interlan NI3210 (Intel 82586) driver.
 */

/*
 * Copyright (C) 1984, Stanford SUMEX project.
 * May be used, but not sold without permission.
 */

/*
 * history
 * 09/06/84	croft	created.
 * 02/05/85	croft	recover after bogus 'all ones' packets
 */


#define	INTIL		0x3	/* interrupt level for il */
#define	splil()		(splabusm1()) /* level 3 also implied here
					since splabus [4] - 1 = 3 */
#define	WAITCMD()	while (scb->sc_cmd) ss->ss_cmdbusy++;

#define	NIL		1	/* number of il interfaces */
#define	NRFD		12	/* number of receive frame descriptors */
#define	RBSZ		320	/* receive buffer size */

#include "gw.h"
#include "pbuf.h"
#include "ether.h"
#include "il.h"


/*
 * Software status, per interface.
 */
struct sstatus {
	struct scb *ss_scb;	/* scb address space */
	struct csregs *ss_cs;	/* address of NI3210 cmd/status */
	struct ether_addr ss_ea;/* ethernet address */
	struct pqueue ss_sendq;	/* send queue */
	struct cb *ss_rfdhead;	/* receive frame descriptors, head */
	struct cb *ss_rfdtail;	/* receive frame descriptors, tail */
	struct bd *ss_rbdhead;	/* receive buffer descriptors, head */
	struct bd *ss_rbdtail;	/* receive buffer descriptors, tail */
	char	ss_oactive;	/* transmit active */
	int	ss_ipackets;	/* total number of input packets */
	int	ss_opackets;	/* total number of output packets */
	int	ss_ierrors;	/* total number of input errors */
	int	ss_oerrors;	/* total number of output errors */
	int	ss_cmdbusy;	/* # of busywaits for scb->sc_cmd */
} ilsstatus[NIL] = {
	{ (struct scb *)0x150000,	(struct csregs *)0x1faaa0 }
};

extern struct ifnet ifil[];


/*
 * Initialize interface.
 */
ilinit(unit)
{
	register struct	sstatus *ss = &ilsstatus[unit];
	register struct	csregs *cs = ss->ss_cs;
	register struct	scb *scb = ss->ss_scb;
	int s;
	register struct cb *rfd;
	register struct	bd *rbd;

	s = splil();
	/*
	 * setup initial SCB
	 */
	cs->cs_1 = INTIL;
	cs->cs_2 = INTIL;
	bzero((caddr_t)scb, SCBLEN);
	bcopy((caddr_t)iliscp, STOA(caddr_t, ISCPOFF), sizeof iliscp);
	CSOFF(0);
	CSOFF(CS_CA);
	while ((cs->cs_2 & CS_CA) == 0);	/* until interrupt asserted */
	if (*STOA(short *, ISCPOFF) != 0 || scb->sc_status != (SC_CX|SC_CNR))
		panic("ilinit: sanity check");
	scb->sc_cmd = (SC_CX|SC_CNR);	/* acknowledge */
	CSOFF(CS_CA);
	while ((cs->cs_2 & CS_CA) != 0);	/* until interrupt drops */
	bzero(STOA(caddr_t, ISCPOFF), sizeof iliscp);
	/*
	 * configure and iasetup
	 */
	bcopy((caddr_t)ilconfig, (caddr_t)&scb->sc_cb, sizeof ilconfig);
	scb->sc_clist = ATOS(&scb->sc_cb);
	ilcmd(unit);	/* configure */
	scb->sc_cb.cb_cmd = (CB_EL|CBC_IASETUP);
	/* odd bcopy count forces byte xfer;  src/dst already swapped. */
	bcopy((caddr_t)&cs->cs_bar, (caddr_t)&scb->sc_cb.cb_param, 7);
	CSOFF(CS_SWAP);
	bcopy((caddr_t)&scb->sc_cb.cb_param, (caddr_t)&ss->ss_ea,
		sizeof ss->ss_ea);
	CSOFF(0);
	ilcmd(unit);	/* iasetup */
	bcopy((caddr_t)&ss->ss_ea, (caddr_t)ifil[unit].if_haddr,
		sizeof ss->ss_ea); /* set ether address in interface struct */
	scb->sc_cb.cb_cmd = (CB_EL|CB_I|CBC_TRANS);  /* leave setup for xmit */
	scb->sc_cb.cb_param = ATOS(&scb->sc_tbd);
	scb->sc_tbd.bd_buf = ATOS(scb->sc_data);
	/*
	 * setup receive unit
	 */
	for (rfd = &scb->sc_rfd[0] ; rfd < &scb->sc_rfd[NRFD] ; rfd++ ) {
		rfd->cb_link = ATOS(rfd+1);
		rfd->cb_param = 0xFFFF;
	}
	rfd--;
	ss->ss_rfdtail = rfd;
	rfd->cb_cmd = CB_EL;
	rfd->cb_link = ATOS(&scb->sc_rfd[0]);
	ss->ss_rfdhead = rfd = &scb->sc_rfd[0];
	rfd->cb_param = ATOS(&scb->sc_rbd[0]);
	for (rbd = &scb->sc_rbd[0] ;
	    (caddr_t)(rbd+1) + RBSZ < (caddr_t)(scb) + SCBLEN ;
	    rbd = (struct bd *)((caddr_t)rbd + sizeof *rbd + RBSZ) ) {
		rbd->bd_next = ATOS((caddr_t)(rbd+1)+RBSZ);
		rbd->bd_size = RBSZ;
		rbd->bd_buf = ATOS(rbd+1);
	}
	rbd = (struct bd *)((caddr_t)rbd - sizeof *rbd - RBSZ);
	ss->ss_rbdtail = rbd;
	ss->ss_rbdhead = &scb->sc_rbd[0];
	rbd->bd_next = ATOS(&scb->sc_rbd[0]);
	rbd->bd_size |= BD_EL;
	scb->sc_rlist = ATOS(&scb->sc_rfd[0]);
	WAITCMD();
	scb->sc_cmd = (SC_CX|SC_CNR|SC_FR|SC_RNR|RUC_START);
	CS(CS_CA);
	splx(s);
}


/*
 * Handle interrupt.
 */
ilintr(unit)
{
	register struct	sstatus *ss = &ilsstatus[unit];
	register struct	csregs *cs = ss->ss_cs;
	register struct	scb *scb = ss->ss_scb;
	int s,scbstatus;
	register struct cb *rfd;
	struct pbuf *p;

	/* spl used to guard against NI3210 edge vs. level trig. intr. */
	s = splil();
	WAITCMD();
	scbstatus = scb->sc_status;
	scb->sc_cmd = (scb->sc_status & (SC_CX|SC_FR|SC_CNR|SC_RNR));
	CS(CS_CA);	/* ack current interrupts */
	WAITCMD();
	if ((scbstatus & SC_RNR) && 
	    ((rfd = ss->ss_rfdhead)->cb_status & CB_B)) {
		/*
		 * Receive unit not ready, yet still busy on 1st frame!
		 * This is a bogus packet of 'infinite' length and all
		 * ones.  Restart the RU.
		 */
		register struct bd *rbd;
		for (rbd = STOA(struct bd *, rfd->cb_param) ;
		    rbd->bd_count & BD_F ;
		    rbd = STOA(struct bd *, rbd->bd_next) ) {
			rbd->bd_count = 0;
		}
		ilrstart(unit);
	}
	if ((scbstatus & SC_FR) == 0)
		goto trycx;	/* if frame not received */
	/*
	 * receive frame.
	 */
	for (rfd = ss->ss_rfdhead ; (rfd->cb_status&CB_C) ;
	    rfd = ss->ss_rfdhead = STOA(struct cb *, rfd->cb_link)) {
		register struct bd *rbd;
		u_char *cp;
		int free, count;

		p = p_get(PT_ENET);
		free = MAXDATA;
		if (p)
			p->p_off = cp = p->p_data;
		for (rbd = STOA(struct bd *, rfd->cb_param) ;
		    rbd->bd_count & BD_F ;
		    rbd = STOA(struct bd *, rbd->bd_next) ) {
			count = (rbd->bd_count & BD_COUNT);
			if (count <= free) {
				CS(CS_SWAP);
				if (p)
					bcopy((caddr_t)(rbd+1), cp, count);
				CS(0);		/* no swap */
				cp += count;
				free -= count;
			} else {	/* buffer overflow */
				if (p) {
					p_free(p);
					p = 0;
					ss->ss_ierrors++;
				}
			}
			if (rbd->bd_count & BD_EOF)
				break;
			rbd->bd_count = 0;
		}
		rbd->bd_count = 0;
		rbd->bd_size |= BD_EL;
		ss->ss_rbdtail->bd_size &= BD_COUNT; /* clear previous EL */
		ss->ss_rbdtail = rbd;
		ss->ss_rbdhead = STOA(struct bd *, rbd->bd_next);
		ss->ss_ipackets++;
		if (p) {
			int ss = splimp();
			p->p_len = MAXDATA - free;
			p_if(p) = &ifil[unit];
			p_enq(&pq, p);
			splx(ss);
		}
		rfd->cb_status = 0;
		rfd->cb_cmd = CB_EL;
		rfd->cb_param = 0xFFFF;
		ss->ss_rfdtail->cb_cmd = 0;	/* clear previous CB_EL */
		ss->ss_rfdtail = rfd;
	}
	ilrstart(unit);	/* kick the receive unit */
trycx:
	if ((scbstatus & SC_CX)) { 
		/*
		 * command (transmit) executed.
		 */
		if (ss->ss_oactive == 0 || (scb->sc_cb.cb_status & CB_C) == 0)
			goto exit;	/* if spurrious interrupt */
		if ((scb->sc_cb.cb_status & CB_OK) == 0)
			ss->ss_oerrors++;
		ss->ss_opackets++;
		ss->ss_oactive = 0;
		if (ss->ss_sendq.pq_head) /* more on queue, restart output */
			ilxstart(unit);
	}
exit:
	splx(s);
}


/*
 * Start or restart output.
 */
ilxstart(unit)
{
	register struct	sstatus *ss = &ilsstatus[unit];
	register struct	csregs *cs = ss->ss_cs;
	register struct	scb *scb = ss->ss_scb;
	register struct pbuf *p;

	if (ss->ss_sendq.pq_head == 0) 
		return;		/* nothing in output queue */
	p = p_deq(&ss->ss_sendq);
	CS(CS_SWAP);
	bcopy(p->p_off, (caddr_t)&scb->sc_data[0], p->p_len);
	CS(0);
	if (p->p_len < 64)
		p->p_len = 64;
	scb->sc_tbd.bd_count = (p->p_len | BD_EOF);
	p_free(p);
	WAITCMD();
	scb->sc_cmd = (SC_CX|SC_CNR|CUC_START);
	CS(CS_CA);
	ss->ss_oactive = 1;
}

	
/*
 * Execute a single command, in scb->sc_cb.
 */
ilcmd(unit)
{
	register struct	sstatus *ss = &ilsstatus[unit];
	register struct	csregs *cs = ss->ss_cs;
	register struct	scb *scb = ss->ss_scb;

	WAITCMD();
	scb->sc_cmd = (SC_CX|SC_CNR|CUC_START);
	CSOFF(CS_CA);
	WAITCMD();
	while ((scb->sc_status & SC_CNR) == 0);
	scb->sc_cmd = (SC_CX|SC_CNR);	/* ack, clear interr */
	CSOFF(CS_CA);
}


/*
 * Start receive unit, if needed.
 */
ilrstart(unit)
{
	register struct	sstatus *ss = &ilsstatus[unit];
	register struct	csregs *cs = ss->ss_cs;
	register struct	scb *scb = ss->ss_scb;

	/* ignore if RU already running or less than 2 elements on lists */
	if ((scb->sc_status & SC_RUS) == RUS_READY)
		return;
	if (ss->ss_rfdhead->cb_cmd & CB_EL)
		return;
	if (ss->ss_rbdhead->bd_size & BD_EL)
		return;
	WAITCMD();
	ss->ss_rfdhead->cb_param = ATOS(ss->ss_rbdhead);
	scb->sc_rlist = ATOS(ss->ss_rfdhead);
	scb->sc_cmd = RUC_START;
	CS(CS_CA);
}


/*
 * Ethernet output routine.
 * Encapsulate a packet of type af for the local net.
 */
iloutput(ifp, p, af, dst)
	struct ifnet *ifp;
	struct pbuf *p;
	caddr_t dst;
{
	int type, s;
	int hln = 6;
	u_char edst[6];
	iaddr_t idst;
	register struct sstatus *ss = &ilsstatus[ifp->if_unit];
	register struct ether_header *eh;

	if (dbsw & DB_ILO)
		printf("ILO\n");
	switch (af) {

	case AF_IP:
		idst = *(iaddr_t *)dst;
		if (!arpresolve(ifp, p, &idst, edst))
			return (0);	/* if not yet resolved */
		type = ETHERTYPE_IPTYPE;
		break;

	case AF_ARP:
		bcopy((caddr_t)dst, (caddr_t)edst, hln);
		type = ETHERTYPE_ARPTYPE;
		break;

	default:
		panic("il%d: can't handle af%d\n", ifp->if_unit, af);
	}

	/*
	 * Add local net header.
	 */
	p->p_off -= sizeof (struct ether_header);
	p->p_len += sizeof (struct ether_header);
	eh = (struct ether_header *)p->p_off;
	eh->ether_type = htons((u_short)type);
	bcopy((caddr_t)edst, (caddr_t)&eh->ether_dhost, hln);
	bcopy((caddr_t)&ss->ss_ea, (caddr_t)&eh->ether_shost, hln);

	/*
	 * Queue message on interface, and start output if interface
	 * not yet active.
	 */
	if (dbsw & DB_ILO)
		p_print("ILO", p);
	s = splil();
	p_enq(&ss->ss_sendq, p);
	if (!ss->ss_oactive)
		ilxstart(ifp->if_unit);
	splx(s);
	return (0);

}


/*
 * Print stats.
 */
ilprintstats(unit)
{
	register struct sstatus *ss = &ilsstatus[unit];
	register struct scb *scb = ss->ss_scb;

	printf("il status unit %d\n", unit);
	printf("in %d, out %d, inerrs %d, outerrs %d\n",
	  ss->ss_ipackets, ss->ss_opackets, ss->ss_ierrors, ss->ss_oerrors);
	printf("scb status 0x%x, errs crc: %d, aln %d, rsc %d, ovrn %d\n",
	  scb->sc_status, scb->sc_crcerrs, scb->sc_alnerrs,
	  scb->sc_rscerrs, scb->sc_ovrnerrs);
}
