module A = Belt.Array;
module O = Belt.Option;

type tableName = string;

module type ColumnType = {
  type t;
  let fromString: string => t;
  let render: t => string;
}

module Column: ColumnType = {
  // e.g. 'foo' or 'mytable.foo'
  type t = string;
  external fromString: string => t = "%identity";
  external render: t => string = "%identity";
};

module type AliasedType = {
  type t('a);
  let make: (~a: string = ?, 'a) => t('a);
  let render: ('a => string, t('a)) => string;
};

module Aliased: AliasedType = {
  type t('a) = ('a, option(string));
  let make = (~a=?, x) => (x, a);
  let render: ('a => string, t('a)) => string =
    renderInner =>
      fun
      | (x, None) => renderInner(x)
      | (x, Some(alias)) => renderInner(x) ++ " AS " ++ alias;
};

module Expression = {
  type atom =
    | Column(Column.t)
    | Int(int)
    | Float(float)
    | String(string)
    | Bool(bool);

  type t =
    | Atom(atom)
    | Typed(t, string)
    | Eq(t, t)
    | Neq(t, t)
    | And(t, t)
    | Leq(t, t)
    | Gt(t, t)
    | Geq(t, t)
    | Or(t, t)
    | Lt(t, t)
    | Call(string, array(t));

  let renderAtom: atom => string =
    fun
    | Column(col) => Column.render(col)
    | Int(i) => string_of_int(i)
    | Float(f) => Js.Float.toString(f)
    | String(s) => "'" ++ s ++ "'" // TODO escape quotes
    | Bool(b) => b ? "TRUE" : "FALSE";

  let rec render: t => string =
    fun
    | Atom(atom) => renderAtom(atom)
    | Typed(e, t) => render(e) ++ "::" ++ t
    | Eq(ex1, ex2) => render(ex1) ++ " = " ++ render(ex2)
    | Neq(ex1, ex2) => render(ex1) ++ " <> " ++ render(ex2)
    | Lt(ex1, ex2) => render(ex1) ++ " < " ++ render(ex2)
    | Leq(ex1, ex2) => render(ex1) ++ " <= " ++ render(ex2)
    | Gt(ex1, ex2) => render(ex1) ++ " > " ++ render(ex2)
    | Geq(ex1, ex2) => render(ex1) ++ " >= " ++ render(ex2)
    | And(ex1, ex2) => render(ex1) ++ " AND " ++ render(ex2)
    | Or(ex1, ex2) => render(ex1) ++ " OR " ++ render(ex2)
    | Call(fnName, args) =>
      fnName ++ "(" ++ Js.Array.joinWith(", ", A.map(args, render)) ++ ")";
};

module Join = {
  type condition = Expression.t;

  type type_ =
    | Inner(condition)
    | Left(condition)
    | Right(condition)
    | Cross;

  let renderType: type_ => (string, option(string)) =
    fun
    | Inner(on) => ("INNER JOIN", Some(Expression.render(on)))
    | Left(on) => ("LEFT OUTER JOIN", Some(Expression.render(on)))
    | Right(on) => ("RIGHT OUTER JOIN", Some(Expression.render(on)))
    | Cross => ("CROSS JOIN", None);
};

module Query = {
  // What comes after the FROM of a query.
  type target =
    | TableName(Aliased.t(tableName))
    | SubQuery(t, string)
    | Join(Join.type_, target, target)

  // Renders into a SELECT query.
  and t = {
    selections: array(Aliased.t(Expression.t)),
    from: option(target),
    groupBy: array(Column.t),
    limit: option(int),
    where: option(Expression.t),
  };

  let rec renderTarget: target => string =
    fun
    | TableName(tname) => Aliased.render(s => s, tname)
    | SubQuery(q, alias) => "(" ++ renderQuery(q) ++ ") AS " ++ alias
    | Join(join, t1, t2) =>
      switch (Join.renderType(join)) {
      | (keyword, None) => renderTarget(t1) ++ " " ++ keyword ++ " " ++ renderTarget(t2)
      | (keyword, Some(on)) =>
        renderTarget(t1) ++ " " ++ keyword ++ " " ++ renderTarget(t2) ++ " ON " ++ on
      }

  and renderQuery: t => string =
    ({selections, from, groupBy, limit, where}) => {
      let groupByString =
        switch (groupBy) {
        | [||] => ""
        | columns => " GROUP BY " ++ Js.Array.joinWith(", ", A.map(columns, Column.render))
        };
      let limitString = O.mapWithDefault(limit, "", n => " LIMIT " ++ string_of_int(n));
      let selectionsString =
        Js.Array.joinWith(", ", A.map(selections, Aliased.render(Expression.render)));
      let fromString = O.mapWithDefault(from, "", t => " FROM " ++ renderTarget(t));
      let whereString = O.mapWithDefault(where, "", e => " WHERE " ++ Expression.render(e));
      "SELECT " ++ selectionsString ++ fromString ++ groupByString ++ whereString ++ limitString;
    };
};

let renderQuery = Query.renderQuery;
