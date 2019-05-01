
-- A small SQL script. Note that no parameterisation is performed.
--
-- Testing that ';' in a comment works as expected.
-- select * from one;
--
-- Like that.

insert into one values (1001, 'script;test');
insert into two values (2001, 1001);

-- Try out the upsert functionality
insert into one values (1001, 'new;value1')
   on conflict (col_a) do update set col_b='new;''value2';


