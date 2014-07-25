
#include "ngsF-HMM.hpp"

// General structure for launching threads
struct pth_struct{
  int type;
  double **Fw;
  double **Bw;
  double **Vi;
  double **data;
  double *F;
  double *aa;
  double ***prior;
  char *path;
  double *pos_dist;
  uint64_t length;
};



// Function prototypes
void threadpool_add_task(threadpool_t *thread_pool, int type, double **Fw, double **Bw, double **Vi, double **data, double *F, double *aa, double ***prior, char *path, double *pos_dist, uint64_t length);
void thread_slave(void *ptr);
double forward(double **Fw, double **data, double F, double aa, double ***prior, char *path, double *pos_dist, uint64_t length);
double backward(double **Bw, double **data, double F, double aa, double ***prior, char *path, double *pos_dist, uint64_t length);
double viterbi(double **Vi, double **data, double F, double aa, double ***prior, char *path, double *pos_dist, uint64_t length);
double lkl(const double *x, const void *ptr);


// General thread functions
void threadpool_add_task(threadpool_t *thread_pool, int type, double **Fw, double **Bw, double **Vi, double **data, double *F, double *aa, double ***prior, char *path, double *pos_dist, uint64_t length){
  pth_struct *p = new pth_struct;

  p->type = type;
  p->Fw = Fw;
  p->Bw = Bw;
  p->Vi = Vi;
  p->data = data;
  p->F = F;
  p->aa = aa;
  p->prior = prior;
  p->path = path;
  p->pos_dist = pos_dist;
  p->length = length;

  // Add task to thread pool
  int ret = threadpool_add(thread_pool, thread_slave, (void*) p, 0);
  if(ret == -1)
    error(__FUNCTION__, "invalid thread pool!");
  else if(ret == -2)
    error(__FUNCTION__, "thread pool lock failure!");
  else if(ret == -3)
    error(__FUNCTION__, "queue full!");
  else if(ret == -4)
    error(__FUNCTION__, "thread pool is shutting down!");
  else if(ret == -5)
    error(__FUNCTION__, "thread failure!");

}

void thread_slave(void *ptr){
  pth_struct* p = (pth_struct*) ptr;

  if(p->type == 1)
    forward(p->Fw, p->data, *p->F, *p->aa, p->prior, p->path, p->pos_dist, p->length);
  else if(p->type == 2)
    backward(p->Bw, p->data, *p->F, *p->aa, p->prior, p->path, p->pos_dist, p->length);
  else if(p->type == 3)
    viterbi(p->Vi, p->data, *p->F, *p->aa, p->prior, p->path, p->pos_dist, p->length);
  else if(p->type == 4){
    double val[2] = {*p->F, *p->aa};
    double l_bound[2] = {0, 0};
    double u_bound[2] = {1, 1};
    int lims[2] = {2, 2};

    findmax_bfgs(2, val, (void*) p, &lkl, NULL, l_bound, u_bound, lims, -1);
    *p->F = val[0];
    *p->aa = val[1];
  }else
    error(__FUNCTION__, "invalid thread task option!");
}



double lkl(const double *pars, const void *data){
  pth_struct* p = (pth_struct*) data;
  double **Fw = init_ptr(p->length+1, N_STATES, 0.0);

  double lkl = forward(Fw, p->data, pars[0], pars[1], p->prior, p->path, p->pos_dist, p->length);

  free_ptr((void**) Fw, p->length+1);
  return -lkl;
}


// Forward function
double forward(double **Fw, double **data, double F, double aa, double ***prior, char *path, double *pos_dist, uint64_t length){
  Fw[0][0] = log(1-F);
  Fw[0][1] = log(F);

  for (uint64_t s = 1; s <= length; s++)
    for(uint64_t l = 0; l < N_STATES; l++){
      double e_l = logsum3(data[s][0]+prior[s][l][0], data[s][1]+prior[s][l][1], data[s][2]+prior[s][l][2]);
      // logsum(k==0,k==1)
      Fw[s][l] = logsum2(Fw[s-1][0] + calc_trans(0,l,pos_dist[s],F,aa),
			 Fw[s-1][1] + calc_trans(1,l,pos_dist[s],F,aa)) + e_l;
    }

  return logsum(Fw[length],2);
}



// Backward function
double backward(double **Bw, double **data, double F, double aa, double ***prior, char *path, double *pos_dist, uint64_t length){
  Bw[length][0] = log(1);
  Bw[length][1] = log(1);

  for (uint64_t s = length; s > 0; s--){
    double e_nIBD = logsum3(data[s][0]+prior[s][0][0], data[s][1]+prior[s][0][1], data[s][2]+prior[s][0][2]);
    double e_IBD  = logsum3(data[s][0]+prior[s][1][0], data[s][1]+prior[s][1][1], data[s][2]+prior[s][1][2]);

    for(uint64_t k = 0; k < N_STATES; k++)
      // logsum(l==0,l==1)
      Bw[s-1][k] = logsum2(calc_trans(k,0,pos_dist[s],F,aa) + e_nIBD + Bw[s][0],
			   calc_trans(k,1,pos_dist[s],F,aa) + e_IBD  + Bw[s][1]);
  }

  Bw[0][0] += log(1-F);
  Bw[0][1] += log(F);

  return logsum(Bw[0],2);
}



// Viterbi function
double viterbi(double **Vi, double **data, double F, double aa, double ***prior, char *path, double *pos_dist, uint64_t length){
  Vi[0][0] = log(1-F);
  Vi[0][1] = log(F);

  for (uint64_t s = 1; s <= length; s++)
    for(uint64_t l = 0; l < N_STATES; l++){
      double e_l = logsum3(data[s][0]+prior[s][l][0], data[s][1]+prior[s][l][1], data[s][2]+prior[s][l][2]);

      // max(k==0,k==1)
      Vi[s][l] = max(Vi[s-1][0] + calc_trans(0,l,pos_dist[s],F,aa),
		     Vi[s-1][1] + calc_trans(1,l,pos_dist[s],F,aa)) + e_l;
    }

  for (uint64_t s = 1; s <= length; s++)
    path[s] = (Vi[s][0] > Vi[s][1] ? 0 : 1);

  return max(Vi[length][0], Vi[length][1]);
}
