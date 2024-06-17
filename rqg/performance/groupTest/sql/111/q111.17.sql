select  count(l_linestatus) 'Count 21 Billion Char(1)s: From ~42 Billion Rows' from lineitem where l_linestatus <> 'O';
