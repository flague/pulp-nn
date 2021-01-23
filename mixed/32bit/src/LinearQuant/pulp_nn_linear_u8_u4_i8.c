/*
 * pulp_nn_linear_u8_u4_i8.c
 * Nazareno Bruschi <nazareno.bruschi@unibo.it>
 *
 * Copyright (C) 2019-2020 University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "pmsis.h"
#include "pulp_nn_utils.h"
#include "pulp_nn_kernels.h"

#define log2(x) __builtin_pulp_fl1(x)
#define min(a,b) ((a)<(b)?(a):(b))
#define SumDotp(a, b, c) __builtin_pulp_sdotusp4(a, b, c)
#define bitins(dst,not_mask_imm,src,mask_imm,off) __builtin_pulp_binsert(dst,not_mask_imm,src,mask_imm,off)
#define bitext(x,size,off) __builtin_pulp_bextract(x,size,off)
#define clip8(x) __builtin_pulp_clipu_r(x, 15)

void pulp_nn_linear_u8_u4_i8(
                  uint8_t *pInBuffer,
                  int8_t *pWeights,
                  uint16_t dim_vec,
                  uint16_t num_o_neurons,
                  int8_t *bias,
                  uint16_t bias_shift,
                  int8_t out_shift,
                  uint16_t out_mult,
                  int32_t *k,
                  int32_t *lambda,
                  uint8_t *pOutBuffer,
                  int flag_relu,
                  int flag_batch_norm,
                  unsigned int * memory_chan
)
{
	int8_t mask = 0xf0;
	int8_t n_mask = ~ mask;
	int8_t off = 0x04;
	uint16_t dim_vec_in = dim_vec;
	uint16_t dim_vec_wt = dim_vec;

	int core_id = pi_core_id();
	int Log2Core = log2(NUM_CORES);
	int chunk = (num_o_neurons >> Log2Core) + ((num_o_neurons & (NUM_CORES-1))!=0);
	int start = min((chunk << (chunk == 1)) * core_id, num_o_neurons);
	int stop = min(start + (chunk << (chunk == 1)), num_o_neurons);

	v4u vecA;
	v4s vecB;
	v4s vecB2;

	uint8_t *pOut = (uint8_t *) pOutBuffer + (start >> 1);

	int i;
	int32_t *k1 = k + start;
	int32_t *lambda1 = lambda + start;

	for(i=start; i<stop; i+=2)
	{
		int sum = 0;
		int sum2 = 0;

		uint8_t *pA = pInBuffer;
		int8_t *pB = pWeights + (i * dim_vec_wt);
		int8_t *pB2 = pB + dim_vec_wt;

		for (int j=0; j<(dim_vec >> 2); j++)
		{
		  vecA = *((v4u*)pA);
		  vecB = *((v4s*)pB);
		  vecB2 = *((v4s*)pB2);
		  sum = SumDotp(vecA, vecB, sum);
		  sum2 = SumDotp(vecA, vecB2, sum2);
	      pA+=4;
	      pB+=4;
	      pB2+=4;
		}
    	uint16_t col_cnt = dim_vec & 0x3;
	    while (col_cnt)
	    {
	      uint8_t inA = *pA;
	      pA++;
	      int8_t inB = *pB;
	      pB++;
	      int8_t inB2 = *pB2;
	      pB2++;
	      sum += inA * inB;
	 	  sum2 += inA * inB2;
      	  col_cnt--;
    	}
	    if (flag_batch_norm && flag_relu)
	    {
	      sum = pulp_nn_bn_quant_u4(sum, *k1, *lambda1, out_shift);
	      sum2 = pulp_nn_bn_quant_u4(sum2, *(k1 + 1), *(lambda1 + 1), out_shift);
	      *pOut = bitins(sum, n_mask, sum2, mask, off);
	      pOut++;
		  k1+=2;
		  lambda1+=2;
    	}
	    else
	    {
	      if (flag_relu == 1)
	      {
	        sum = pulp_nn_quant_u4(sum, out_mult, out_shift);
	        sum2 = pulp_nn_quant_u4(sum2, out_mult, out_shift);
	        *pOut = bitins(sum, n_mask, sum2, mask, off);
	        pOut++;
	      }
	      else
	      {
	        sum = (uint8_t) clip8(sum >> out_shift);
	        sum2 = (uint8_t) clip8(sum2 >> out_shift);
	        *pOut = bitins(sum, n_mask, sum2, mask, off);
	        pOut++;
      	  }
    	}
	}
	pi_cl_team_barrier(0);
}
