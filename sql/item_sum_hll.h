#ifndef ITEM_SUM_HLL_INCLUDED
#define ITEM_SUM_HLL_INCLUDED


#include "sql_priv.h"                /* STRING_BUFFER_USUAL_SIZE */
#include "unireg.h"
#include "sql_const.h"                 /* RAND_TABLE_BIT, MAX_FIELD_NAME */
#include "unireg.h"                    // REQUIRED: for other includes
#include "thr_malloc.h"                         /* sql_calloc */
#include "field.h"                              /* Derivation */
#include "sql_array.h"
#include "item.h"
#include "item_sum.h"

class Item_sum_count_hll :public Item_sum_int
{
  longlong count;

  void clear();
  bool add();
  void cleanup();

  public:
   Item_sum_count_hll(Item *item_par)
    :Item_sum_int(item_par),count(0)
  {
    hll_init();
  }

  Item_sum_count_hll(List<Item> &list)
      :Item_sum_int(list),count(0)
  {
    set_distinct(FALSE);
    hll_init();
  }

  Item_sum_count_hll(THD *thd, Item_sum_count_hll *item)
    :Item_sum_int(thd, item), count(item->count)
  {
    hll_init();
  }

  ~Item_sum_count_hll(){
    delete [] data;
  }


  enum Sumfunctype sum_func () const
  {
    return COUNT_DISTINCT_FUNC;
  }
  void no_rows_in_result() { count=0; }
  void make_const(longlong count_arg)
  {
    count=count_arg;
    Item_sum::make_const();
  }


  longlong val_int();
  void reset_field();
  void update_field();

  const char *func_name() const
  {
    return "hll(";
  }
  Item *copy_or_same(THD* thd);

  private:
  void hll_init();
  void hll_reset();
  void hll_insert(uint hash);
  longlong hll_count();

  double get_harmonic_mean_constant(uint data_size);

  uchar data_size_log2;
  uint data_size;
  uchar max_bit_position;
  uint * data;

};

#endif
