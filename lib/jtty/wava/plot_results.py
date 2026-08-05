
import pandas as pd
import matplotlib.pyplot as plt

# Read data logged from the Fortran testbench suite
try:
    df = pd.read_csv('simulation_results.csv')
except FileNotFoundError:
    print("Error: 'simulation_results.csv' not found. Execute the Fortran simulation program first.")
    exit()

# Strip whitespace from columns to avoid parsing mismatches
df['channel'] = df['channel'].str.strip()

# Initialize Figure with high resolution DPI properties
fig, ax1 = plt.subplots(figsize=(10, 6), dpi=120)

# Set up Primary Axis: Frame Correct-Decode Probability P(Correct)
ax1.set_xlabel('SNR in 2500 Hz Reference Bandwidth [dB]', fontsize=11, fontweight='bold')
ax1.set_ylabel('P(Correct Decode)', color='navy', fontsize=11, fontweight='bold')
ax1.set_xlim(0, -20)  # Invert axis view to scan standard drop off descending to -20 dB
ax1.set_ylim(-0.05, 1.05)
ax1.grid(True, which='both', linestyle='--', alpha=0.5)

# Set up Secondary Axis on the right side for 100 * UER (%)
ax2 = ax1.twinx()
ax2.set_ylabel('Undetected Error Rate: 100 * UER [%]', color='crimson', fontsize=11, fontweight='bold')
ax2.set_ylim(-0.1, 5.0)  # Standard percentage visualization window

# Isolate channel tracking configurations
awgn_data = df[df['channel'] == 'AWGN'].sort_values(by='snr_db', ascending=False)
rayleigh_data = df[df['channel'] == 'RAYLEIGH'].sort_values(by='snr_db', ascending=False)

# Plot Primary Curves - P(Correct)
line1 = ax1.plot(awgn_data['snr_db'], awgn_data['p_correct'], 
                 'o-', color='navy', linewidth=2.0, label='AWGN: P(Correct)')
line2 = ax1.plot(rayleigh_data['snr_db'], rayleigh_data['p_correct'], 
                 's-', color='royalblue', linewidth=1.8, label='Rayleigh: P(Correct)')

# Plot Secondary Curves - 100 * UER (%)
line3 = ax2.plot(awgn_data['snr_db'], awgn_data['uer_pct'], 
                 '^--', color='crimson', linewidth=1.5, label='AWGN: 100*UER')
line4 = ax2.plot(rayleigh_data['snr_db'], rayleigh_data['uer_pct'], 
                 'v--', color='orange', linewidth=1.5, label='Rayleigh: 100*UER')

# Align Legend structures coherently across shared dual axis space
lines = line1 + line2 + line3 + line4
labels = [l.get_label() for l in lines]
ax1.legend(lines, labels, loc='center left', frameon=True, shadow=True)

plt.title('TBCC (92,46) Noncoherent 4-FSK Performance Profile\nConstraint Length K=11, List Size L=8, Rate=1/2', 
          fontsize=12, fontweight='bold', pad=15)

plt.tight_layout()
plt.savefig('tbcc_performance_curves.png', dpi=300)
print("Plot successfully rendered and saved as 'tbcc_performance_curves.png'")
plt.show()
