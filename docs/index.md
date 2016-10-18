---
layout: default
---

# Bedrock -- Rock-solid distributed data
Bedrock is a simple, modular, WAN-replicated data foundation for global-scale applications.  Taking each of those in turn:

* **Bedrock is simple.** This means it exposes the fewest knobs necessary, with appropriate defaults at every layer.
* **Bedrock is modular.**  This means its functionality is packaged into separate "plugins" that are decoupled and independently maintainable.
* **Bedrock is WAN-replicated.**  This means it is designed to endure the myriad real-world problems that occur across slow, unreliable internet connections.
* **Bedrock is a data foundation.**  This means it is not just a simple database that responds to queries, but rather a platform on which data-processing applications (like databases, job queues, caches, etc) can be built.
* **Bedrock is for global-scale applications.**  This means it is built to be deployed in a geo-redundant fashion spanning many datacenters around the world.

Bedrock was built by [Expensify](https://www.expensify.com), and is a networking and distributed transaction layer built atop [SQLite](http://sqlite.org/), the fastest, most reliable, and most widely distributed database in the world.

## Why to use it
If you're building a website or other online service, you've got to use *something*.  Why use Bedrock rather than the alterantives?  We've provided a more [detailed comparision against MySQL](http://bedrockdb.com/vs_mysql.html), but in general Bedrock is:

* **Faster.**  This is true for networked queries using the Bedrock::DB plugin, but especially true for custom plugins you write yourself because SQLite is just a library the operates inside your process's memory space.  That means when your plugin queries SQLite, it isn't serializing/deserializing over a network: it's directly accessing the RAM of the database itself.  This is great in a single node, but if you still want more (becuase who doesn't?) then install any number of nodes and load-balance reads across all of them.  This means every CPU of every database server is available for parallel reads, each of which has direct access to the database RAM.

* **Simpler.**  This is because Bedrock is written for modern hardware with large SSD-backed RAID drives and generous RAM file caches, and thereby doesn't mess with the zillion hacky tricks the other databases do the eek out high performance on largely obsolete hardware.  This results in fewer esoteric knobs, and sane defaults that "just work".

* **More reliable.**  This is because Bedrock can be clustered not just inside a single datacenter, but across multiple datacenters spanning the internet.  This means Bedrock continues functioning not only if a single node goes down, but even if you lose an entire datacenter.  After all, it doesn't matter who you are using: your datacenter *will fail*, eventually.  But you needn't fail along with it.

* **More powerful.**  Most people don't realize just how powerful SQLite is.  Indexes, triggers, foreign key constraints, native JSON support, expression indexes -- check the [full list here](http://sqlite.org/fullsql.html).  You'll be amazed, but that's just the start.  On top of this Bedrock layers a robust plugin system, and includes a fully functional [job queue](http://bedrockdb.com/jobs.html) and [replicated cache](http://bedrockdb.com/cache.html) -- all the basics you need for modern service design, wrapped into one simple package.

Bedrock is not only production ready, but actively used by Expensify's many thousands of customers, and millions of users.  (Curious why an expense reporting company built their own database?  Read what the [First Round Review](http://firstround.com/review/your-database-is-your-prison-heres-how-expensify-broke-free/) has to say about it.)

## How to get it
Bedrock can be compiled from source using the [Expensify/Bedrock](https://github.com/Expensify/Bedrock) public repo, or installed into your Ubuntu environment using the following commands:  (Go ahead and copy/paste this whole section and copy it into your Ubuntu terminal.)

    # Add the Bedrock repo to apt sources:
    sudo wget -O /etc/apt/sources.list.d/bedrock.list https://apt.bedrockdb.com/ubuntu/dists/trusty/bedrock.list

    # Add the Bedrock repo key:
    wget -O - https://apt.bedrockdb.com/bedrock.gpg | sudo apt-key add -

    # Update the apt-get and install Bedrock
    sudo apt-get update
    sudo apt-get install bedrock

## How to use it
Bedrock is so easy to use, you'll think you're missing something.  Once installed, Bedrock listens on `localhost` port 8888, and stores its database in `/var/lib/bedrock`.  The easiest way to talk with Bedrock is using `netcat` as follows:

    $ nc localhost 8888
    Query: SELECT 1 AS foo, 2 AS bar;

That query can be any [SQLite-compatible query](http://sqlite.org/lang.html) -- including schema changes, foreign key constraints, partial indexes, native JSON expressions, or any of the tremendous amount of functionality SQLite offers.  The result will be returned in an HTTP-like response format:

    200 OK
    Content-Length: 16
    
    foo | bar
    1 | 2

By default, Bedrock optimizes the output for human consumption.  If you are a robot, request JSON output:

    $ nc localhost 8888
    Query
    query: SELECT 1 AS foo, 2 AS bar;
    format: json

    200 OK
    Content-Length: 16

    {...}

Some people are creeped out by sockets, and prefer tools.  No problem: Bedrock supports the MySQL protocol, meaning you can continue using whatever MySQL client you prefer:

    $ mysql
    ...

That also means you can continue using whatever MySQL language binding you already know and love.  Alternatively, if you don't know or love any of them, Bedrock also provides a [PHP binding](https://github.com/Expensify/Bedrock-PHP) that looks something like this:

    $bedrock = new Bedrock();
    $result = $bedrock->db->query("SELECT 1 AS foo, 2 AS bar;");

It really can be that easy.

## Bedrock plugins
Additionally, Bedrock::DB is just one plugin to the overall Bedrock platform.  Bedrock itself is less a database, and more a tool that can be used to build a wide variety of data-management applications -- with a database being just one example.  Each "plugin" implements and exposes new externally-visible commands (essentially equivalent to "stored procdures").  However, unlike simple stored procedures, plugins can also include schema changes.  Plugins can be enabled via the "-plugins" command like parameter.  Current plugins include:

* [Status](http://bedrockdb.com/status.html) - Provides basic status about the health the Bedrock cluster.
* [DB](http://bedrockdb.com/db.html) - Provides direct SQL access to the underlying database.
* [Jobs](http://bedrockdb.com/jobs.html) - Provides a simple job queue.
* [Cache](http://bedrockdb.com/cache.html) - Provides a simiple replicated cache.
* [MySQL](http://bedrockdb.com/mysql.html) - Emulates MySQL

## How to get help
So many ways!

* Run `bedrock -?` on the command line to see all the available [command-line options](http://bedrockdb.com/cli.html)
* Chat with us live on [Bedrock's Gitter page](https://gitter.im/Expensify-Bedrock/Lobby)
* Post to the [Bedrock mailing list](https://groups.google.com/forum/#!forum/bedrock) by emailing [bedrock@googlegroups.com](mailto:bedrock@googlegroups.com)
* Create an issue in [Bedrock's GitHub issue list](https://github.com/Expensify/Bedrock/issues)
* Email David, the CEO of Expensify (and biggest Bedrock fanboy ever) directly: [dbarrett@expensify.com](mailto:dbarrett@expensify.com)

