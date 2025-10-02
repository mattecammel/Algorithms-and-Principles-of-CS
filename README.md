Pastry Shop Order Management Simulator

Score: 30/30 (Final Exam in Algorithms and Data Structures 2023–2024)

Overview

This program simulates the operation of an industrial pastry shop. It manages recipes, ingredients, customer orders, and periodic deliveries via a courier truck. The simulation is discrete-time based: every processed input command advances the time by one unit.

The program is designed to read commands from standard input and print results to standard output, respecting the behavior defined in the exam specification.

Features

Recipes:

Add or remove recipes, each with a unique name and a list of required ingredients with quantities.

Recipes already in use by pending orders cannot be removed.

Ingredients & Warehouse:

Ingredients are stored in batches with specific expiration times.

Expired batches are automatically discarded.

Batches are consumed in order of earliest expiration (FIFO by expiry).

Orders:

Customers place orders for recipes.

Orders are either prepared immediately if ingredients are available, or queued until replenishment.

Orders are always processed chronologically.

Courier Deliveries:

The truck arrives at fixed time intervals with limited capacity.

Orders are loaded in chronological order until capacity is reached.

Loading priority: heavier orders first; ties are resolved by order arrival time.

If no orders are ready, the program outputs camioncino vuoto.

Input Format

The input is a sequence of lines:

First Line: two integers

<truck_period> <truck_capacity>


truck_period: how often the truck arrives (in time units).

truck_capacity: maximum weight (grams) the truck can carry.

Commands (one per line):

Add Recipe

aggiungi_ricetta <recipe_name> <ingredient_name> <quantity> ...


Output: aggiunta or ignorato.

Remove Recipe

rimuovi_ricetta <recipe_name>


Output: rimossa, ordini in sospeso, or non presente.

Replenish Ingredients

rifornimento <ingredient_name> <quantity> <expiration> ...


Output: rifornito.

Place Order

ordine <recipe_name> <quantity>


Output: accettato or rifiutato.

At each truck arrival time (multiples of truck_period), the program outputs the orders loaded on the truck as:

<order_time> <recipe_name> <quantity>


If no orders can be shipped:

camioncino vuoto

Output Examples

Example Input

3 500
aggiungi_ricetta torta zucchero 200 farina 300 uova 100
ordine torta 2
rifornimento zucchero 1000 5 farina 1000 5 uova 1000 5
ordine torta 1


Possible Output

aggiunta
rifiutato
rifornito
accettato
camioncino vuoto
0 torta 1

Build Instructions

Compile with a C compiler (e.g., GCC or Clang):

gcc -O2 -Wall -o pastry_simulator pastry_simulator.c


Run the program with input redirection:

./pastry_simulator < input.txt > output.txt

Debugging Options

The program contains several debug macros that can be enabled by uncommenting the #define lines at the top of the source code:

DebugUsage → prints memory/time usage (Windows only).

DebugHashTable → prints hash table collision info.

DebugProfile → prints profiling counters for insertions and batch operations.

DebugInput, DebugQueue, DebugIngredients, DebugDelivery → detailed internal state traces.

Limitations

Ingredient and recipe names: up to 255 characters, using [a-zA-Z_].

All quantities are integers > 0.

Maximum ingredient batches: 512 (circular buffer).

Input line length: up to 32,768 characters.

Author

Developed as part of an academic exercise in Algorithms and Data Structures (2023–2024).
Final grade: 30/30.
