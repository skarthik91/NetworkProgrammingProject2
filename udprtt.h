#ifndef	__udp_rtt_h
#define	__udp_rtt_h
/* debug flag; can be set by caller */
#include	"unpifiplus.h"

struct rtt_info {
    int		rtt_rtt;	/* most recent measured RTT, in seconds */
    int		rtt_srtt;	/* smoothed RTT estimator, in seconds */
    int		rtt_rttvar;	/* smoothed mean deviation, in seconds */
    int		rtt_rto;	/* current RTO to use, in seconds */
    int		rtt_nrexmt;	/* # times retransmitted: 0, 1, 2, ... */
    uint32_t	rtt_base;	/* # sec since 1/1/1970 at start */
};


#define	RTT_RXTMIN     1/* min retransmit timeout value, in mseconds */
#define	RTT_RXTMAX     3 /* max retransmit timeout value, in mseconds */
#define	RTT_MAXNREXMT 	12	/* max # times to retransmit */
#define RTT_RTOCALC(ptr) ((ptr)->rtt_srtt + (4.0 * (ptr)->rtt_rttvar))

/* function prototypes */
void	 rtt_debug1(struct rtt_info *);
void	 rtt_init1(struct rtt_info *);
void	 rtt_newpack1(struct rtt_info *);
int		 rtt_start1(struct rtt_info *);
void	 rtt_stop1(struct rtt_info *, uint32_t);
int		 rtt_timeout1(struct rtt_info *);
uint32_t rtt_ts1(struct rtt_info *);

extern int	rtt_d_flag1;	/* can be set to nonzero for addl info */

#endif	/* __unp_rtt_h */
