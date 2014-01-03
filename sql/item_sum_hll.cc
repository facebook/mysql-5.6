#include "item_sum_hll.h"


#include "sql_priv.h"
#include "sql_select.h"
#include "sql_tmp_table.h"                 // create_tmp_table
#include "sql_resolver.h"                  // setup_order, fix_inner_refs
#include "sql_optimizer.h"                 // JOIN
#include "mysqld.h"

Item *Item_sum_count_hll::copy_or_same(THD* thd)
{
  return new (thd->mem_root) Item_sum_count_hll(thd, this);
}

void Item_sum_count_hll::clear()
{
  count = 0;
}

bool Item_sum_count_hll::add()
{
  for (uint i = 0; i < arg_count; i++)
  {
    if (args[i]->maybe_null && args[i]->is_null())
      return 0;
  }
  String tempStr;
  String *val = args[0]->val_str_ascii(&tempStr);

  uint32 hashCode = my_sbox_hash((uchar*)val->ptr(),val->length());
  hll_insert(hashCode);
  return 0;
}

longlong Item_sum_count_hll::val_int()
{
  count = hll_count();
  return (longlong) count;
}


void Item_sum_count_hll::cleanup()
{
  DBUG_ENTER("Item_sum_count_hll::cleanup");
  count = 0;
  Item_sum_int::cleanup();
  DBUG_VOID_RETURN;
}

void Item_sum_count_hll::reset_field()
{
  uchar *res = result_field->ptr;
  longlong nr = 0;
  DBUG_ASSERT (aggr->Aggrtype() != Aggregator::DISTINCT_AGGREGATOR);

  if (!args[0]->maybe_null || !args[0]->is_null())
    nr = 1;
  int8store(res,nr);
}


void Item_sum_count_hll::update_field()
{
  longlong nr;
  uchar *res = result_field->ptr;

  nr = sint8korr(res);
  if (!args[0]->maybe_null || !args[0]->is_null())
    nr++;
  int8store(res,nr);
}

//Implementation of hyperloglog algorithm follows
const double long_range_adjustment_constant32 = 4.294967296e9;

// alpha_m in the hyperloglog paper. Refer to the comment in hyperloglog.h
double Item_sum_count_hll::get_harmonic_mean_constant(uint data_size) {
  if (data_size >= 128) {
    return 0.7213 / (1.079 / data_size + 1.0);
  } else if (data_size == 16) {
    return 0.673;
  } else if (data_size == 32) {
    return 0.697;
  } else if (data_size == 64) {
    return 0.709;
  }
  //Should never reach here.
  return 0;
}

void Item_sum_count_hll::hll_init(){
  THD* thd = current_thd;

  data_size_log2 = thd->variables.hll_data_size_log2;
  data_size=1 << data_size_log2;
  data=new uint[data_size];
  memset(data, 0, data_size*sizeof(uint));
}

void Item_sum_count_hll::hll_reset(){
  memset(data, 0, data_size * sizeof(uint));
}

void Item_sum_count_hll::hll_insert(uint hash){
  uint last_len = 32 - data_size_log2;
  uint index = hash >> last_len;
  uint last_bit = hash - (index << last_len);
  uint i;
  for (i = 1; i <= last_len; i++) {
    if (last_bit & 1) {
      break;
    } else {
      last_bit >>= 1;
    }
  }
  if(data[index] < i){
    data[index] = i;
  }
}

longlong Item_sum_count_hll::hll_count(){
  double harmonic_mean_constant = get_harmonic_mean_constant(data_size);
  double sum = 0.0;
  double cardinality_estimate = 0.0;
  uint count_zero_elements = 0;

  for(uint i = 0; i < data_size; i++){
    if(data[i] == 0){
      count_zero_elements++;
    }

    sum += 1.0 / ((uint)1 << data[i]);
  }
  cardinality_estimate = harmonic_mean_constant * data_size * data_size / sum;

  if(cardinality_estimate <= 2.5 * data_size) {
    if(count_zero_elements != 0){
      cardinality_estimate =
        log((double)data_size / count_zero_elements) * data_size;
    }
  }else if (cardinality_estimate > long_range_adjustment_constant32 / 30.0) {
    cardinality_estimate = -long_range_adjustment_constant32 *
      log(1.0 - cardinality_estimate / long_range_adjustment_constant32);
  }
  return (longlong)(cardinality_estimate + 0.5);
}
