
-- A small SQL script. Note that no parameterisation is performed.
--

insert into table one values (1001, 'script;test');
insert into table two values (2001, 1001);

-- Try out the upsert functionality
insert into table one values (1001, 'new;value1')
   on conflict (col_a) do update set col_b='new;value2';


